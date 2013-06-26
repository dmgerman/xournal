project(ttsubset)

add_definitions(-DNO_MAPPERS -DNO_TYPE3 -DNO_TYPE42)

add_library(ttsubset sft.c sft.h list.c list.h ttcr.c ttcr.h)
