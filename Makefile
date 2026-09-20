CC     = gcc
FLEX   = flex
BISON  = bison
CFLAGS = -std=c99 -Wall -g -Isrc

# Task 2 会补上 src/tree.c
SRCS   = src/main.c src/report.c
OBJS   = $(SRCS:.c=.o)

parser: $(OBJS) syntax.tab.o lex.yy.o
	$(CC) -o $@ $^ -lfl
	cp -f parser cc

syntax.tab.c: src/syntax.y
	$(BISON) -o syntax.tab.c -d -v src/syntax.y

# flex 需要 syntax.tab.h，故依赖 syntax.tab.c（bison -d 会一并生成该头文件）
lex.yy.c: src/lexical.l syntax.tab.c
	$(FLEX) -o lex.yy.c src/lexical.l

syntax.tab.o: syntax.tab.c src/tree.h src/report.h
	$(CC) $(CFLAGS) -c -o $@ syntax.tab.c

lex.yy.o: lex.yy.c src/tree.h src/report.h
	$(CC) $(CFLAGS) -c -o $@ lex.yy.c

%.o: %.c src/tree.h src/report.h
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	rm -f parser cc *.o src/*.o lex.yy.c syntax.tab.c syntax.tab.h syntax.output
