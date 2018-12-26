#ifndef __ENC_OPS__
#define __ENC_OPS__

#define STREM_USE_BUFF      0x01
#define STREM_MULT_BUFF     0x02

enum {
    ENC_NO_DATA = 0x01,
    ENC_OUT_ERR,
    ENC_RUN_ERR,
    ENC_PARM_ERR,
};

typedef struct encoder_strem_ops {
   int (*out_put)(void *priv, void *buff, int len);
   void *(*outbuf_alloc)(void *priv, int *len);
   void (*outbuf_finish)(void *priv, void *buf, int len);
   int (*in_put)(void *priv, void *buff, int len);
   void *(*inbuf_alloc)(void *priv, int *len);
   void (*inbuf_finish)(void *priv, void *buf, int len);
}encoder_strem_ops;


typedef struct encoder_inf {
    unsigned int sample : 30;
    unsigned int nch    : 2;
    encoder_strem_ops *strem_ops;
    void *priv;
    unsigned char audio_strem_type;
}encoder_inf;

typedef struct encoder_ops {
    /* int (*get_needbuf)(); */
    void *(*init)(void *buf, encoder_inf *inf);
    int (*run)(void *priv);
    int (*get_head)(void *priv);
}encoder_ops;


typedef struct encoder_ops_inf {
    int need_buff : 24;
    int audio_strem_type : 8;
    encoder_ops *ops;
}encoder_ops_inf;

int wav_encode_get_opsinf(encoder_ops_inf *inf, int audio_strem_type);
#endif
