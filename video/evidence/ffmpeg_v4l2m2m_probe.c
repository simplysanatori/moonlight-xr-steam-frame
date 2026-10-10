// Moonlight XR Native - evidence: what the host FFmpeg 7.0 v4l2m2m decoders expose.
// Build: clang -o probe ffmpeg_v4l2m2m_probe.c -lavcodec -lavutil; run with env -u LD_LIBRARY_PATH.
// Copyright (C) 2026 Moonlight XR Native contributors. GPLv3, see LICENSE.txt.
#include <stdio.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
int main(void){
  const char *names[]={"h264_v4l2m2m","hevc_v4l2m2m"};
  for(int n=0;n<2;n++){
    const AVCodec *c=avcodec_find_decoder_by_name(names[n]);
    printf("%s: %p caps=0x%x\n",names[n],(void*)c,c?c->capabilities:0);
    if(!c)continue;
    printf("  pix_fmts:");
    if(c->pix_fmts) for(const enum AVPixelFormat *p=c->pix_fmts;*p!=AV_PIX_FMT_NONE;p++) printf(" %s",av_get_pix_fmt_name(*p)); else printf(" (null)");
    printf("\n  hw_configs:");
    int i=0; const AVCodecHWConfig *h;
    while((h=avcodec_get_hw_config(c,i++))) printf(" [pix=%s methods=0x%x devtype=%d]",av_get_pix_fmt_name(h->pix_fmt),h->methods,h->device_type);
    printf(" count=%d\n",i-1);
    const AVOption *o=NULL; printf("  priv opts:");
    while(c->priv_class && (o=av_opt_next(&c->priv_class,o))) printf(" %s",o->name);
    printf("\n");
  }
  printf("avcodec %s\n", av_version_info());
}
