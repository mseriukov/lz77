@echo off
mkdir test 2>nul >nul
pushd test
if not exist "ut.h" (
	curl -LJO https://raw.githubusercontent.com/leok7v/ui/main/single_file_lib/ut/ut.h
)
if not exist "ui.h" (
	curl -LJO https://raw.githubusercontent.com/leok7v/ui/main/single_file_lib/ui/ui.h
)
if not exist "sqlite3.c" (
	curl -LJO https://raw.githubusercontent.com/jmscreation/libsqlite3/main/src/sqlite3.c
)
popd

