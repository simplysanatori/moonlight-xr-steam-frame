#!/bin/sh
# tests/m6: the synthetic 3D scene for mlxr-m6-pipeline --analyse: a far brick
# wall, a checker floor in perspective, a shaded red ball rolling across the
# front (H.264 1080p30, 14 s). Usage: gen_scene.sh [outdir]. GPLv3.
set -e
out=${1:-/tmp/claude-1000/m6}; mkdir -p "$out"; cd "$out"
# Far back wall (bricks) above y=400, a checker floor in perspective below
ffmpeg -y -hide_banner -loglevel error -f lavfi -i nullsrc=s=1920x1080:d=1 -frames:v 1 -vf "format=rgb24,geq=r='if(gt(Y,400),if(mod(floor((X-960)/(Y-399)*6)+floor(2400/(Y-399)),2),205,70),if(mod(floor((X+20*mod(floor(Y/22),2))/44)+floor(Y/22),2),120,95))':g='if(gt(Y,400),if(mod(floor((X-960)/(Y-399)*6)+floor(2400/(Y-399)),2),205,70),if(mod(floor((X+20*mod(floor(Y/22),2))/44)+floor(Y/22),2),130,105))':b='if(gt(Y,400),if(mod(floor((X-960)/(Y-399)*6)+floor(2400/(Y-399)),2),205,70),if(mod(floor((X+20*mod(floor(Y/22),2))/44)+floor(Y/22),2),160,135))'" bg.png
# A shaded red ball, lit from the top left
ffmpeg -y -hide_banner -loglevel error -f lavfi -i nullsrc=s=360x360:d=1 -frames:v 1 -vf "format=rgba,geq=r='clip(40+215*(0.25+0.75*max(0,(-(X-180)*0.45-(Y-180)*0.45)/178+0.6*sqrt(max(0,1-((X-180)*(X-180)+(Y-180)*(Y-180))/31684)))),0,255)':g='clip(10+40*max(0,(-(X-180)*0.45-(Y-180)*0.45)/178),0,255)':b='clip(10+30*max(0,(-(X-180)*0.45-(Y-180)*0.45)/178),0,255)':a='if(lt(hypot(X-180,Y-180),178),255,0)'" ball.png
ffmpeg -y -hide_banner -loglevel error -loop 1 -framerate 30 -i bg.png -loop 1 -framerate 30 -i ball.png -filter_complex "[0][1]overlay=x='780+520*sin(t*0.9)':y=640,format=yuv420p" -t 14 -r 30 -c:v libx264 -preset fast -crf 18 -g 30 -bf 0 scene.mp4
echo ok
