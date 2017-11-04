# sqlite3-dbf
Converter of XBase/FoxPro tables to SQLite

# About

SQLiteDBF converts XBase databases, particularly FoxPro tables with memo files, into a SQL dump. It has no dependencies other than standard Unix libraries.
This use codebase of the PgDBF project (http://pgdbf.sourceforge.net/) which designed to be incredibly fast and as efficient as possible.

# Usage

When terminal encoding is same to dBase file encoding the usage is very simple:

```sqlite3-dbf test.dbf | sqlite3 test.db```

Use iconv or similar tool for Cyrillic dBase files and UTF-8 console encoding:

```LANG="ru_RU.CP866" sqlite3-dbf test.dbf | iconv -f cp866 -t utf8 | sqlite3 test.db```

Call the utility without command-line arguments to see some additional options:

```
$ sqlite3-dbf

Usage: sqlite3-dbf [-m memofilename] filename [indexcolumn ...]
Convert the named XBase file into SQLite format

  -h  print this message and exit
  -m  the name of the associated memo file (if necessary)
```

# History

The project moved from my own fossil repository.
