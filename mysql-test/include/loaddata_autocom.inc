# Test if the engine does autocommit in LOAD DATA INFILE, or not

eval SET SESSION STORAGE_ENGINE = $engine_type;

--disable_warnings
drop table if exists t1;
--enable_warnings

let $load_file= $MYSQLTEST_VARDIR/std_data/loaddata2.dat;

create table t1 (id int unsigned not null auto_increment primary key, a text, b text);
start transaction;
--replace_result $load_file LOAD_FILE
eval load data infile '$load_file' into table t1 fields terminated by ',' enclosed by '''' (a, b);
commit;
select count(*) from t1;
truncate table t1;
start transaction;
--replace_result $load_file LOAD_FILE
eval load data infile '$load_file' into table t1 fields terminated by ',' enclosed by '''' (a, b);
rollback;
select count(*) from t1;

drop table t1;
