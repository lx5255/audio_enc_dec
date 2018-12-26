#include <stdio.h>
#include <stdlib.h>
#include "encoder_ops.h"

struct encoder_hd{
	void *encoder;
	encoder_ops_inf ops_inf;
	void *in_file;
	void *out_file;
};

struct  encoder_hd hdl;

int out_buf(void *priv, void *buf, int len)
{
	int ret;
	struct encoder_hd *hdl = priv;
	if(hdl == NULL){
		return 0;
	}

	ret = fwrite((void *)buf,  1, len, hdl->out_file);
	printf("write file size %d hd %x file %x buf %x ret %d\n", len, hdl, hdl->out_file, buf, ret);
	return ret;
}

int int_buf(void *priv, void *buf, int len)
{
	struct encoder_hd *hdl = priv;
	if(hdl == NULL){
		return 0;
	}
	printf("read file size %d  hd %x file %x\n", len, hdl, hdl->in_file);
	return  fread((void *)buf,  1, len, hdl->in_file);
}

encoder_strem_ops  ops_io =
{
	.out_put = out_buf,
	.in_put = int_buf,
};

int main(int argc, char *argv[])
{
	//encoder_ops_inf ops_inf;
	int ret;
	encoder_inf inf;
	void *need_buf;
    printf("Hello world!\n");
     printf("int size %d\n", sizeof(int));

	//hdl.in_file = fopen((void *)argv[1],"r");
	//hdl.out_file = fopen((void *)argv[2],"rb");

	hdl.in_file = fopen((void *)"1.pcm","rb");
	if(hdl.in_file == NULL){
		printf("open in file err\n");
		return -1;
	}
	//hdl.out_file = fopen((void *)"1.wav","rb");

	hdl.out_file = fopen((void *)"1.wav",  "wb+");
	if(hdl.out_file == NULL) {
		printf("new file err\n");
		return -1;
	}

	printf("out %x in %x\n", hdl.out_file , hdl.in_file);


	if(wav_encode_get_opsinf(&hdl.ops_inf, 0x01)){
		printf("get ops err\n");
		return -1;
	}
	printf("need buf len %d strem type %d\n", hdl.ops_inf.need_buff, hdl.ops_inf.audio_strem_type);
	need_buf = malloc(hdl.ops_inf.need_buff);
	if(need_buf == NULL){
		printf(" malloc err!\n");
		return -1;
	}
	inf.audio_strem_type = hdl.ops_inf.audio_strem_type;
	inf.nch = 2;
	inf.priv = &hdl;
	inf.sample = 44100;
	inf.strem_ops = &ops_io;
	hdl.encoder = hdl.ops_inf.ops->init(need_buf, &inf);
	if(hdl.encoder == NULL){
		printf(" open encoder err!\n");
		return -1;
	}

	hdl.ops_inf.ops->get_head(hdl.encoder);


	while(1){
		ret = hdl.ops_inf.ops->run(hdl.encoder);
		if(ret){
			printf(" open encoder run err %d!\n", ret);
			break;
		}
	}
	fseek(hdl.out_file, 0, 0);
	hdl.ops_inf.ops->get_head(hdl.encoder);

    return 0;
}
