

function libcurl_build {
  pushDir $WORK/src

  lazy_download "libcurl.tar.gz" "https://curl.haxx.se/download/curl-7.58.0.tar.bz2"
  lazy_extract "libcurl.tar.gz"

  autoconf_build $host "libcurl"

  popDir
}

function libcurl_get_deps {
  local a=0
}

