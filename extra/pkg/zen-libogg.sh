
function libogg_build {
  host=$1
  pushDir $WORK/src

  lazy_download "libogg.tar.gz" "http://downloads.xiph.org/releases/ogg/libogg-1.3.1.tar.gz"
  lazy_extract "libogg.tar.gz"
  mkgit "libogg"

  autoconf_build $host "libogg"
  popDir
}

function libogg_get_deps {
  echo ""
}


