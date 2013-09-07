.PHONY: all pzcc clean
.DEFAULT: all

CC = gcc -g 
LD = g++
LLVMFLAGS = $(shell llvm-config --cflags)
CFLAGS += -Wall $(LLVMFLAGS)
#-I"/usr/include/llvm-c-3.2" -I"/usr/include/llvm-3.2"
LDFLAGS += -lgc
#When run as previous, it throws "undefined reference to dladdr" during linking. That is because -ldl must appear before -lLLVMSupport.
LLVM_LINK_FLAGS=$(shell llvm-config --libs core analysis native ; llvm-config --cflags --ldflags)
OBJ += pzc.lex.o semantic.o parser.o symbol.o general.o error.o ir.o termopts.o
DEPENDS += 

ifndef DEBUG
       DEBUG = n
endif

ifeq ($(DEBUG),y)
	CFLAGS += -O0 -g
	BSN_DBG += -t
else
	CFLAGS += -O3
endif

all: pzcc clean

pzcc: $(OBJ) 
	$(LD) $(OBJ) $(LDFLAGS) $(LLVM_LINK_FLAGS) -o $@

termopts.o: termopts.c termopts.h
	$(CC) $(CFLAGS) -c $< -o $@
general.o: general.c general.h
	$(CC) $(CFLAGS) -c $< -o $@
semantic.o: semantic.c semantic.h
	$(CC) $(CFLAGS) -c $< -o $@
error.o: error.c error.h
	$(CC) $(CFLAGS) -c $< -o $@
symbol.o: symbol.c symbol.h general.h
	$(CC) $(CFLAGS) -c $< -o $@
parser.o: parser.c parser.h 
	$(CC) $(CFLAGS) -c $< -o $@

parser.h: parser.c 

parser.c: parser.y semantic.h
		bison ${BSN_DBG} -v -d -o $@ $<

pzc.lex.o: pzc.lex.c semantic.h parser.h symbol.h
	$(CC) $(CFLAGS) -c -lfl $< -o $@

pzc.lex.c: pzc.lex
	flex -s -o $@ $< 
pzc.lex:;

distclean: clean
	rm -f pzcc

clena celan lcean lcena: clean
clean:
	rm -f $(OBJ) pzc.lex.c a.out parser.c parser.h tests/*/*.imm tests/*/*.asm parser.output

install:
	cp pzcc /usr/bin

uninstall:
	rm /usr/bin/pzcc
