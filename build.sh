[ -f ./a.out ] && rm ./a.out
gcc *.c ../collections/src/*.c ../strings/src/*.c ../numbers/src/*.c ../files/src/*.c ../json/src/*.c ../graphs/src/*.c -I../collections/include -I../strings/include -I../files/include -I../numbers/include -I../graphs/include -I../json/include -g -Werror -lm
[ -f ./a.out ] && ./a.out
