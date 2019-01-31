
function ffmpeg_build {
  host=$1

  lazy_download "ffmpeg.tar.gz" "http://ffmpeg.org/releases/ffmpeg-4.0.tar.bz2"
  lazy_extract "ffmpeg.tar.gz"
  mkgit "ffmpeg"

  (
    cd ffmpeg
    ffmpeg_patches
  )

  local ARCH=$(get_arch $host)
  local OS=$(get_os $host)

  local os=$OS
  case $OS in
    darwin*)
      os="darwin"
      ;;
  esac

  # remove stupid dependency
  $sed -i "s/jack_jack_h pthreads/jack_jack_h/" ffmpeg/configure

  mkdir -p ffmpeg/build/$host
  pushDir ffmpeg/build/$host

  CFLAGS="-I$PREFIX/include" \
  LDFLAGS="-L$PREFIX/lib" \
  ../../configure \
      --prefix=$PREFIX \
      --enable-pthreads \
      --disable-w32threads \
      --disable-debug \
      --disable-doc \
      --disable-static \
      --enable-shared \
      --enable-librtmp \
      --enable-gpl \
      --enable-libx264 \
      --enable-zlib \
      --disable-programs \
      --disable-gnutls \
      --disable-openssl \
      --disable-iconv \
      --disable-bzlib \
      --enable-avresample \
      --disable-decoder=mp3float \
      --pkg-config=pkg-config \
      --target-os=$os \
      --arch=$ARCH \
      --cross-prefix=$host-
  $MAKE
  $MAKE install
  popDir
}

function ffmpeg_get_deps {
  echo librtmp
  echo libpthread
  echo x264
  echo zlib
}


function ffmpeg_patches {
  local patchFile=$scriptDir/patches/ffmpeg_01_hlsenc_lower_verbosity.diff
  cat << 'EOF' > $patchFile
diff --git a/libavformat/hlsenc.c b/libavformat/hlsenc.c
index c27a66e..6bfb175 100644
--- a/libavformat/hlsenc.c
+++ b/libavformat/hlsenc.c
@@ -2206,7 +2206,7 @@ static int hls_write_packet(AVFormatContext *s, AVPacket *pkt)
             if (pkt->duration) {
                 vs->duration += (double)(pkt->duration) * st->time_base.num / st->time_base.den;
             } else {
-                av_log(s, AV_LOG_WARNING, "pkt->duration = 0, maybe the hls segment duration will not precise\n");
+                av_log(s, AV_LOG_VERBOSE, "pkt->duration = 0, maybe the hls segment duration will not precise\n");
                 vs->duration = (double)(pkt->pts - vs->end_pts) * st->time_base.num / st->time_base.den;
             }
         }
EOF

  applyPatch $patchFile
}
