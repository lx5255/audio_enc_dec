#include <stdio.h>
#include <stdlib.h>
#include "encoder_ops.h"

#define SYS_BIG_EDIAN   0 
#define POINT_SIZE      2
#define POINT_PBLOCK    8
#define BLOCK_SIZE      512 
/* #define SAM_PBLOCK      ((512)*2)  */

#define AUDIO_STREM_TYPE    STREM_USE_BUFF

#define CACHE_BUFF_SIZE     512

#if (WAVE_ADPCM==0x11)
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
    short wBitsPerSample;   //量化数
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
#else
#define RID_OFFSET          0
#define RLEN_OFFSET         4
#define FLEN_OFFSET         16
#define FORMAT_OFFSET       20
#define NCH_OFFSET          22
#define SAMP_OFFSET         24
#define AVGB_OFFSET         28
#define BAL_OFFSET          32
#define BPSA_OFFSET         34
#define OTHER_OFFSET        36
#define F2LEN_OFFSET        74 
#define DLEN_OFFSET         78 
#define WSALEN_OFFSET       86 

#pragma pack(1)
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
    short wBitsPerSample;   //量化数
    char other[34];
    //FACT
    char f2id[4];              //"fact"
    int f2len;
    int dataLen;          //解码成PCM后数据长度
    //DATA
    char dId[4];              //"data"
    int wSampleLength;    //音频数据的大小
} TWavHeader __attribute__((packed));
#pragma pack()
#endif
/* typedef struct { */
/*    short cur_value; */
/*    short diff_value; */
/*    short idDelta;  */
/* }adpcm_parm; */
/*  */
typedef struct {
   int val_pre;
   /* short idDelta;  */
   /* unsigned short block_cnt; */
   char index; 
   char dir;
   short idDelta;
}ADPCM_STA;


typedef struct wav_encode {
   TWavHeader *head;
   encoder_strem_ops *ops;
   void *priv;
   void *buff;
   unsigned char *bbuf;
   unsigned char *pbuf;
   short cur_value_l;
   short cur_value_r;
   short diff_value_l;
   short diff_value_r;
   short idDelta_l; 
   short idDelta_r; 
   ADPCM_STA adpcm_l;
   ADPCM_STA adpcm_r;
   unsigned short block_cnt;
   unsigned char strem_ops_type;
   unsigned char nch;
   unsigned char big_edian; 
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

static int indexTable[16] = {   
    -1, -1, -1, -1, 2, 4, 6, 8,   
    -1, -1, -1, -1, 2, 4, 6, 8,   
};   

static unsigned int stepsizeTable[89] = {   
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,   
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,   
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,   
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,   
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,   
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,   
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,   
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,   
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767   
}; 

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
    printf("head %d\n", sizeof(TWavHeader));
    if(head){
        memset(head, 0, sizeof(TWavHeader));
        memcpy(head->rId, "RIFF", 4);
        memcpy(head->wId, "WAVE", 4);
        memcpy(head->fId, "fmt ", 4);
        memcpy(head->f2id, "fact", 4);
        memcpy(head->dId, "data", 4);
        head->fLen = 20;
        head->wFormatTag = WAVE_ADPCM;
        head->nChannels = nch;
        head->nSamplesPerSec  = sample;
        head->nAvgBytesPerSec = (sample * nch * BLOCK_SIZE)/((BLOCK_SIZE - 4)*2 + 1);
        head->nBlockAlign     = BLOCK_SIZE;
        head->wBitsPerSample  = POINT_SIZE * 2;
        head->f2len = 4;
#if (WAVE_ADPCM == 2)
        head->other[0] = 0x20;
#else 
        head->nbSize = 0x02;
        head->nsamplesperblock = (BLOCK_SIZE - 4 * nch)*2 + 1;
#endif
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
#if (WAVE_ADPCM == 2)
#else
   st_dword_func(&buf[BSIZE_OFFSET], head->nbSize);
   st_dword_func(&buf[SAPB_OFFSET], head->nsamplesperblock);
#endif
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
    encode->adpcm_l.index = 0;
    encode->adpcm_r.index = 0;
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
    /* printf("%s %d\n", __func__, __LINE__); */
    if(wlen != encode->ops->out_put(encode->priv, encode->bbuf, wlen)){
        printf("out err\n");
        return ENC_OUT_ERR;
    }
    encode->head->wSampleLength += wlen;
    return 0;
}


int wav_enc_block(ADPCM_STA *adpcm_sta, short *in, int *out); 
static int wav_encode_run(void *priv)
{
    int rlen, wlen, point_cnt;
    block_point *out_buf;
    short *in_buf;
    wav_encode *encode = priv;
    TWavHeader *header = encode->head;
    short pcm_data[POINT_PBLOCK];

    if(encode == NULL){
        return ENC_PARM_ERR;
    }

    in_buf = encode->pbuf;
    out_buf = &encode->bbuf[encode->block_cnt*4 * encode->nch];

    if(encode->nch == 2){

    } else{
        if(encode->block_cnt == 0){ //写块头
            rlen = encode->ops->in_put(encode->priv, in_buf, POINT_SIZE);
            if(rlen != POINT_SIZE){
                printf("%s %d\n", __func__, __LINE__);
                return ENC_NO_DATA;
            }
            MonoBlockHeader *bheadr = out_buf;
            bheadr->sample0 = *in_buf;
            /* bheadr->sample0 = encode->adpcm_l.val_pre; */
            bheadr->index = encode->adpcm_l.index;
            /* bheadr->sample0 = encode->adpcm_l.val_pre; */
            out_buf = encode->bbuf + sizeof(MonoBlockHeader);
            int diff =  encode->adpcm_l.val_pre - bheadr->sample0;
            if((diff> 200)||(diff < -200)) {
                printf("pre samp %d, samp0 %d diff %d step %d\n", encode->adpcm_l.val_pre, bheadr->sample0, diff, bheadr->index);
            }
            encode->adpcm_l.idDelta = 4; 
            /* if(diff<0){ */
            /*     encode->adpcm_l.val_pre = bheadr->sample0 - stepsizeTable[bheadr->index]/(encode->adpcm_l.idDelta *2);  */
            /* }else{ */
            /*     encode->adpcm_l.val_pre = bheadr->sample0 + stepsizeTable[bheadr->index]/(encode->adpcm_l.idDelta *2);  */
            /* } */
            encode->adpcm_l.val_pre = bheadr->sample0;
            /* encode->adpcm_l.val_pre = 0;  */
            encode->block_cnt++; 
        }

        rlen = encode->ops->in_put(encode->priv, in_buf, POINT_PBLOCK * POINT_SIZE); 
        if(rlen != POINT_PBLOCK * POINT_SIZE){
            printf("%s %d\n", __func__, __LINE__);
            return ENC_NO_DATA;
        }
        wav_enc_block(&encode->adpcm_l, in_buf, out_buf++); 
        encode->head->dataLen+=4*POINT_SIZE;

        if(++encode->block_cnt == (BLOCK_SIZE - 4)/4 + 1){   //一个块127个段，再加一个起始采样点
            encode->block_cnt = 0;
            if(wav_out_block(encode)){
                return ENC_OUT_ERR;
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
        header->rLen = header->wSampleLength + sizeof(TWavHeader);
        printf("header->rLen %d wSampleLength %d\n", header->wSampleLength);
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
            inf->need_buff += BLOCK_SIZE * 2 + POINT_PBLOCK * 2 * 2 + 10*1024;
        }
        inf->ops = &wav_ops;
        return 0;
    }
    return -1;
}

  

int wav_enc_block(ADPCM_STA *adpcm_sta, short *in, int *out) 
{
   unsigned int ad_val; 
   int diff_val;
   int pre_diff;
   int step;
   char sign;
   int i;
  
   *out = 0; 

   /* printf("enc block\n"); */
   for(i = 0; i<8; i++){
       *out >>= 4;
       *out &= 0xfffffff;
       diff_val = *in++ - adpcm_sta->val_pre; 
       /* printf("[diff %d]", diff_val); */
       if(diff_val < 0){
           diff_val = -diff_val;
           sign = 0x8;  
       }else{
           sign = 0x0;  
       }
        /* Note:  
        ** This code *approximately* computes:  
        **    s_diff 通过表换算出来的差值，跟步进有关
        **    ad_val = s_diff*4/step;  
        **    vpdiff = (ad_val+0.5)*step/4;  
        **    vpdiff = s_diff + step/8
        **      
        */
 
       step = stepsizeTable[adpcm_sta->index];
       /* printf("index %d diff %d step %d\n", adpcm_sta->index, diff_val, step); */
#if  0 
       pre_diff = step>>3;
       if(diff_val >= step){
           ad_val |= 0x4;
           diff_val -= step;
           pre_diff += step;
       }
       step >>= 1;
       if(diff_val >= step){
           ad_val |= 0x2;
           diff_val -= step;
           pre_diff += step;
       }
       step >>= 1;
       if(diff_val >= step){
           ad_val |= 0x1;
           diff_val -= step;
           pre_diff += step;
       }
#else
        ad_val = (diff_val * adpcm_sta->idDelta)/step;  
        if(ad_val > 7){
            ad_val = 7;
        }
        /* pre_diff = ad_val*step/4 + step/11; */
        /* pre_diff = diff_val   */
        pre_diff = ad_val*step/adpcm_sta->idDelta + step/(adpcm_sta->idDelta *2);
#endif
       if(sign&0x8){
           adpcm_sta->val_pre -= pre_diff; 
           if (adpcm_sta->val_pre < -32768)   
               adpcm_sta->val_pre = -32768;   
       }else{
           adpcm_sta->val_pre += pre_diff; 
           if (adpcm_sta->val_pre > 32768)   
               adpcm_sta->val_pre = 32768;   
       }
       adpcm_sta->index += indexTable[ad_val];
       if(adpcm_sta->index > sizeof(stepsizeTable)/sizeof(int) - 1){
            adpcm_sta->index = sizeof(stepsizeTable)/sizeof(int) - 1;
       }
       if(adpcm_sta->index < 0){
          adpcm_sta->index = 0;     
       }
       ad_val |= sign;
       *out |= ((int)ad_val)<<28; 
        /* printf("[ad:%x][eo:%x]", ad_val, *out); */
   }
   /* printf("[eo:%x]", *out); */
   return 0;
   /* adpcm_sta->block_cnt++; */
}


