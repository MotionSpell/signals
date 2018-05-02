
function sqlite3_build {
  pushDir $WORK/src

  lazy_download "sqlite3.tar.gz" "https://www.sqlite.org/2018/sqlite-autoconf-3230100.tar.gz"
  lazy_extract "sqlite3.tar.gz"

  autoconf_build $host "sqlite3"

  popDir
}

function sqlite3_get_deps {
  local a=0
}

