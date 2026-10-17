# Проверка VitaTTS без vitasdk (как в соседних репозиториях): make -C host check
#   check      - строгая проверка синтаксиса main.c с заглушками SDK (сигнатуры как в vitasdk)
#   check-all  - то же с HAVE_SAM (прототипы SAM объявлены в main.c); Pico/eSpeak-ветки требуют их заголовки
CFLAGS = -std=gnu11 -D_GNU_SOURCE -Istub -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
STRICT = -Werror=implicit-function-declaration -Werror=int-conversion -Werror=incompatible-pointer-types

check:
	gcc -fsyntax-only $(STRICT) $(CFLAGS) ../main.c

check-all: check
	gcc -fsyntax-only $(STRICT) $(CFLAGS) -DHAVE_SAM ../main.c
