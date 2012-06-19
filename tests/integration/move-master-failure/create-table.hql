use '/';
drop table if exists LoadTest;
create table LoadTest (
  Field MAX_VERSIONS=1
) COMPRESSOR="none";
