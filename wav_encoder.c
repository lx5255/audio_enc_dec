#include <stdio.h>
#include <stdlib.h>
#include "encoder_ops.h"

#define POINT_SIZE      2

#define AUDIO_STREM_TYPE    STREM_USE_BUFF

#define CACHE_BUFF_SIZE     512

#define RID_OFFSET          0
#define RLEN_OFFSET         4
#define FLEN_OFFSET         16
#define FORMAT_OFFSET       20
#define NCH_OFFSET          22
#define SAMP_OFFSET         24
#define AVGB_OFFSET         28
#define BAL_OFFSET          32
#define BPSA_OFFSET         34
#define BSIZE_OFFSET        36
#define SAPB_OFFSET         38
#define F2LEN_OFFSET        44
#define DLEN_OFFSET         48
#define WSALEN_OFFSET       56

typedef struct _TWavHeader {
    char rId[4];    //标志符（RIFF）
    int rLen;   //数据大小,包括数据头的大小和音频文件的大小
    char wId[4];    //格式类型（"WAVE"）
    char fId[4];    //"fmt"
    int fLen;   //Sizeof(WAVEFORMATEX)
    //WAVEFORMATEX
    short wFormatTag;       //编码格式，包括WAVE_FORMAT_PCM，WAVEFORMAT_ADPCM等
    short nChannels;        //声道数，单声道为1，双声道为2
    int nSamplesPerSec;   //采样频率
    int nAvgBytesPerSec;  //每秒的数据量
    short nBlockAlign;      //块对齐
    short wBitsPerSample;   //WAVE文件的采样大小
    short nbSize;
    short nsamplesperblock;
    //FACT
    char f2id[4];              //"fact"
    int f2len;
    int dataLen;          //解码成PCM后数据长度
    //DATA
    char dId[4];              //"data"
    int wSampleLength;    //音频数据的大小
} TWavHeader ;

typedef struct wav_encode {
   TWavHeader *head;
   encoder_strem_ops *ops;
   void *priv;
   void *buff;
   unsigned int out_cnt;
   unsigned int strem_ops_type : 8;
   //unsigned int init :2;
   unsigned int rever : 24;
}wav_encode;

static void st_word_func(char *ptr, short val)
{
    ptr[0] = val;
    ptr[1] = val >> 8;
}

static void st_dword_func(char *ptr, int val)
{
    ptr[0] = val;
    ptr[1] = val >> 8;
    ptr[2] = val >> 16;
    ptr[3] = val >> 24;
}


short ld_word_func(char *p)
{
    return (short)p[1] << 8 | p[0];
}

int ld_dword_func(char *p)
{
    return (int)p[3] << 24 | p[2] << 16 | p[1] << 8 | p[0];
}


static void wav_head_creat(TWavHeader *head, int sample, int nch)
{
    if(head){
        memset(head, 0, sizeof(TWavHeader));
        memcpy(head->rId, "RIFF", 4);
        memcpy(head->wId, "WAVE", 4);
        memcpy(head->fId, "fmt ", 4);
        memcpy(head->f2id, "fact", 4);
        memcpy(head->dId, "data", 4);
        head->fLen = 20;
        head->wFormatTag = 1;
        head->nChannels = nch;
        head->nSamplesPerSec  = sample;
        head->nAvgBytesPerSec = sample * nch * POINT_SIZE;
        head->nBlockAlign     = nch * POINT_SIZE;
        head->wBitsPerSample  = POINT_SIZE * 8;
        head->f2len = 4;
        head->nbSize = 2;
    }
}

static int wav_head_to_buf(TWavHeader *head, char *buf)
{
   if(buf == NULL){
       return -1;
   }
   memcpy(buf, head, sizeof(TWavHeader));
   st_dword_func(&buf[RLEN_OFFSET], head->rLen);
   st_dword_func(&buf[FLEN_OFFSET], head->fLen);
   st_word_func(&buf[FORMAT_OFFSET], head->wFormatTag);
   st_word_func(&buf[NCH_OFFSET], head->nChannels);
   st_dword_func(&buf[SAMP_OFFSET], head->nSamplesPerSec);
   st_dword_func(&buf[AVGB_OFFSET], head->nAvgBytesPerSec);
   st_word_func(&buf[BAL_OFFSET], head->nBlockAlign);
   st_word_func(&buf[BPSA_OFFSET], head->wBitsPerSample);
   st_dword_func(&buf[F2LEN_OFFSET], head->f2len);
   st_dword_func(&buf[DLEN_OFFSET], head->dataLen);
   st_dword_func(&buf[WSALEN_OFFSET], head->wSampleLength);
   st_dword_func(&buf[BSIZE_OFFSET], head->nbSize);
   return sizeof(TWavHeader);
}

static void *wav_encode_init(void *buf, encoder_inf *inf)
{
    wav_encode *encode;
    TWavHeader *header;
    if((inf == NULL)||(buf == NULL)){
        return NULL;
    }
    encode = buf;
    memset(encode, 0, sizeof(wav_encode));
    header = buf + sizeof(wav_encode);
    wav_head_creat(header, inf->sample, inf->nch);
    encode->ops = inf->strem_ops;
    if(inf->audio_strem_type & STREM_USE_BUFF){

       encode->buff = buf + sizeof(wav_encode) + sizeof(TWavHeader);
      printf("STREM_USE_BUFF %x\n", encode->buff);
    }
    encode->strem_ops_type = inf->audio_strem_type & AUDIO_STREM_TYPE;
    encode->priv = inf->priv;
    encode->head = header;
    return encode;
}


static int wav_encode_run(void *priv)
{
    int rlen, wlen;
    void *in_buf, *out_buf;
    wav_encode *encode = priv;
    TWavHeader *header = encode->head;

    if(encode == NULL){
        return ENC_PARM_ERR;
    }


    if(encode->strem_ops_type & STREM_MULT_BUFF){
        in_buf = encode->ops->inbuf_alloc(encode->priv, &rlen);
        if((rlen == 0)||(in_buf == NULL)){
            return ENC_NO_DATA;
        }
    } else{
        in_buf = encode->buff;
        rlen = encode->ops->in_put(encode->priv, in_buf, CACHE_BUFF_SIZE);
        if(rlen == 0){
            return ENC_NO_DATA;
        }
        out_buf = in_buf;
    }


    //encoder

__out_put:
    if(encode->strem_ops_type & STREM_MULT_BUFF){
        out_buf = encode->ops->outbuf_alloc(encode->priv, &wlen);
        if((wlen == 0)||(out_buf == NULL)){
            return ENC_OUT_ERR;
        }
        if(wlen < rlen){
            rlen = wlen;
        }
        memcpy(out_buf, in_buf, rlen);
        encode->ops->inbuf_finish(encode->priv, in_buf, rlen);
        encode->ops->outbuf_finish(encode->priv, out_buf, rlen);
        return 0;
    } else{
        wlen = encode->ops->out_put(encode->priv, out_buf, rlen);
        if(rlen != wlen){
            return ENC_OUT_ERR;
        }
    }
    header->wSampleLength += wlen;
    header->dataLen += wlen;

    return 0;

}

static void wav_get_header(void *priv)
{
    wav_encode *encode = priv;
    TWavHeader *header = encode->head;
    void *buf;
    int wlen;

    if((encode == NULL)||(header == NULL)){
       return ;
    }

    header->rLen = header->wSampleLength + sizeof(TWavHeader);


     if(encode->strem_ops_type & STREM_MULT_BUFF){

    } else{
        buf = encode->buff;
        wlen = wav_head_to_buf(header, buf);
        wlen = encode->ops->out_put(encode->priv, buf, wlen);
    }
}

const static encoder_ops wav_ops = {
   .init = wav_encode_init,
   .run  = wav_encode_run,
   .get_head = wav_get_header,
};

int wav_encode_get_opsinf(encoder_ops_inf *inf, int audio_strem_type)
{
    if(inf){
        inf->audio_strem_type = AUDIO_STREM_TYPE & audio_strem_type;
        if(audio_strem_type == 0){
            return -2;
        }
        inf->need_buff = sizeof(TWavHeader) + sizeof(wav_encode);
        printf("header size %d hd size %d\n", sizeof(TWavHeader), sizeof(wav_encode));
        if(inf->audio_strem_type & STREM_USE_BUFF){
            inf->need_buff += CACHE_BUFF_SIZE;
        }
        inf->ops = &wav_ops;
        return 0;
    }
    return -1;
}

