#!/usr/bin/env python

import sys

def main():
    print "# generated by tokudb_update_decr_floor.py"
    print "source include/have_tokudb.inc;"
    print "source include/have_innodb.inc;"
    print "set default_storage_engine='tokudb';"
    print "disable_warnings;"
    print "drop table if exists t;"
    print "enable_warnings;"

    print "set tokudb_disable_slow_update=1;"

    for t in [ 'tinyint', 'smallint', 'mediumint', 'int', 'bigint' ]:
        for u in [ '', 'unsigned' ]:
            for n in [ 'null', 'not null' ]:
                test_int(t, u, n)
    return 0

def test_int(t, u, n):
    print "create table tt ("
    print "    id %s %s %s primary key," % (t, u, n)
    print "    x %s %s %s" % (t, u, n)
    print ");"

    print "insert into tt values (1,4);"
    print "create table ti like tt;"
    print "alter table ti engine=innodb;"
    print "insert into ti select * from tt;"

    if u == 'unsigned':
        print "update noar tt set x=if(x=0,0,x-1) where id=1;"
        print "update noar ti set x=if(x=0,0,x-1) where id=1;"

        print "update noar tt set x=if(x=0,0,x-1) where id=1;"
        print "update noar ti set x=if(x=0,0,x-1) where id=1;"

        print "update noar tt set x=if(x=0,0,x-1) where id=1;"
        print "update noar ti set x=if(x=0,0,x-1) where id=1;"

        print "update noar tt set x=if(x=0,0,x-1) where id=1;"
        print "update noar ti set x=if(x=0,0,x-1) where id=1;"

        print "# try to decrement when x=0"
        print "update noar tt set x=if(x=0,0,x-1) where id=1;"
        print "update noar ti set x=if(x=0,0,x-1) where id=1;"
        print "let $diff_tables = test.tt, test.ti;"
        print "source include/diff_tables.inc;"
    else:
        print "replace_regex /MariaDB/XYZ/ /MySQL/XYZ/;"
        print "error ER_UNSUPPORTED_EXTENSION;"
        print "update noar tt set x=if(x=0,0,x-1) where id=1;"

    print "drop table tt, ti;"

sys.exit(main())
