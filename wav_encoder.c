#include <stdio.h>
#include <stdlib.h>
#include "encoder_ops.h"

#define SYS_BIG_EDIAN   1
#define POINT_SIZE      2
#define POINT_PBLOCK    8
#define BLOCK_SIZE      512 
#define SAM_PBLOCK      ((512 - 4)*2) 

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
   unsigned char *bbuf;
   unsigned char *pbuf;
   short cur_value_l;
   short cur_value_r;
   unsigned short block_cnt;
   unsigned char strem_ops_type;
   unsigned char nch;
}wav_encode;

typedef struct
{
    short sample0;//block中第一个采样值（未压缩）
    unsigned char index; //上一个block最后一个index，第一个block的index=0;
    unsigned char reserved; //尚未使用
}MonoBlockHeader;

typedef struct
{
    MonoBlockHeader leftbher;
    MonoBlockHeader rightbher;
}StereoBlockHeader;

typedef struct {
    int reserved2 : 24;
    int reserved1 : 4;
    int sample0 : 4;
}block_point;

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
        head->wFormatTag = 2;
        head->nChannels = nch;
        head->nSamplesPerSec  = sample;
        head->nAvgBytesPerSec = sample * nch * POINT_SIZE;
        head->nBlockAlign     = nch * 4;
        head->wBitsPerSample  = POINT_SIZE * 8;
        head->f2len = 4;
        head->nbSize = 32;
        head->nsamplesperblock = (BLOCK_SIZE - 4)*2 + 1;
    }
}

static int wav_head_to_buf(TWavHeader *head, char *buf)
{
   if(buf == NULL){
       return -1;
   }
   memcpy(buf, head, sizeof(TWavHeader));
#if SYS_BIG_EDIAN
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
   st_dword_func(&buf[SAPB_OFFSET], head->nsamplesperblock);
#endif
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
      encode->pbuf = header + sizeof(TWavHeader);
      encode->bbuf = encode->pbuf + POINT_PBLOCK * 2 * 2;
    }
    encode->strem_ops_type = inf->audio_strem_type & AUDIO_STREM_TYPE;
    encode->priv = inf->priv;
    encode->head = header;
    encode->nch = inf->nch;
   /*
    block_point data_test;
    data_test.sample0 = 0xf;
    printf("data test %x\n", *(int *)&data_test);
    printf("data test 1 %x\n", *(char *)&data_test);
    printf("data test 2 %x\n", *(((char *)&data_test) + 1));
    printf("data test 3 %x\n", *(((char *)&data_test) + 2));
    printf("data test 4 %x\n", *(((char *)&data_test) + 3));
    while(1);
        */
    return encode;
}

static int wav_out_block(wav_encode *encode)
{
    int wlen;
    wlen = encode->nch == 2?BLOCK_SIZE*2:BLOCK_SIZE;
    if(wlen != encode->ops->out_put(encode->priv, encode->bbuf, wlen)){
        return ENC_OUT_ERR;
    }
    encode->head->wSampleLength += wlen;
    return 0;
}

static int wav_encode_run(void *priv)
{
    int rlen, wlen, point_cnt;
    block_point *out_buf;
    short *in_buf;
    wav_encode *encode = priv;
    TWavHeader *header = encode->head;
    short sam_point;

    if(encode == NULL){
        return ENC_PARM_ERR;
    }

    in_buf = encode->pbuf;
    out_buf = &encode->bbuf[encode->block_cnt/2 * encode->nch];
    rlen = encode->ops->in_put(encode->priv, in_buf, POINT_PBLOCK * POINT_SIZE * 2);
    if(rlen != POINT_PBLOCK * POINT_SIZE * 2){
        return ENC_NO_DATA;
    }

    //encoder
    if(encode->nch == 2){
        for(point_cnt = 0; point_cnt < POINT_PBLOCK; point_cnt++){
            if(encode->block_cnt == 0){ //写块头
                StereoBlockHeader *bheadr = out_buf;
                bheadr->rightbher.sample0 = *in_buf++;
                bheadr->leftbher.sample0 = *in_buf++;
                bheadr->rightbher.index++;
                bheadr->leftbher.index++;
                out_buf = encode->bbuf + sizeof(StereoBlockHeader);
                encode->cur_value_l = bheadr->leftbher.sample0;
                encode->cur_value_r = bheadr->rightbher.sample0;
                encode->block_cnt += 8; 
                printf("new block cur_value_l %d  cur_value_r %d\n", encode->cur_value_l, encode->cur_value_r);
                continue;
            }

            sam_point = *(in_buf + 1) - encode->cur_value_l;
            if(sam_point > 7){
                sam_point = 7;
            }else if(sam_point < -7){
                sam_point = -7;
            }
            out_buf->sample0 = sam_point;
            encode->cur_value_l += sam_point;

            sam_point = *in_buf - encode->cur_value_r;
            if(sam_point > 7){
                sam_point = 7;
            }else if(sam_point < -7){
                sam_point = -7;
            }
            (out_buf + 1)->sample0 = sam_point;
            encode->cur_value_r += sam_point;

            printf("cur sam %d in sam %d sam_point 0x%x out_buf 0x%x\n", encode->cur_value_r, *in_buf, sam_point, *(int *)(out_buf + 1));
            in_buf+=2; 
            if(++encode->block_cnt %8 == 0){  //完成一个 wrod
                printf("block_cnti %d\n", encode->block_cnt); 
                out_buf+=2; 
            }else{
                *((int *)out_buf)>>=4;
                *((int *)(out_buf + 1))>>=4;
            }
            if(encode->block_cnt == SAM_PBLOCK){
                if(wav_out_block(encode)){
                    return ENC_OUT_ERR;
                }
                encode->block_cnt = 0;
            }
        }
    }else{
        for(point_cnt = 0; point_cnt < POINT_PBLOCK*2; point_cnt++){
            if(encode->block_cnt == 0){ //写块头
                MonoBlockHeader *bheadr = out_buf;
                bheadr->sample0 = *in_buf++;
                bheadr->index++;
                out_buf = encode->bbuf + sizeof(MonoBlockHeader);
                encode->cur_value_l = bheadr->sample0;
                /* encode->block_cnt++; */
                encode->block_cnt += 8; 
                continue;
            }

            sam_point = *in_buf - encode->cur_value_l;
            if(sam_point > 7){
                sam_point = 7;
            }else if(sam_point < -7){
                sam_point = -7;
            }
            out_buf->sample0 = sam_point;
            printf("cur sam %d in sam %d sam_point 0x%x out_buf 0x%x", encode->cur_value_l, *in_buf, sam_point, *(int *)out_buf);
            in_buf++; 
            if(++encode->block_cnt %8 == 0){  //完成一个 wrod
                out_buf++; 
            }else{
                *((int *)out_buf)>>=4;
            }
            if(encode->block_cnt == SAM_PBLOCK){
                if(wav_out_block(encode)){
                    return ENC_OUT_ERR;
                }
                encode->block_cnt = 0;
            }
        }
    }
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

