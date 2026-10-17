# Проверка VitaTTS без vitasdk (как в соседних репозиториях): make -C host check
#   check      - строгая проверка синтаксиса main.c с заглушками SDK (сигнатуры как в vitasdk)
#   check-all  - то же с HAVE_SAM и HAVE_RHVOICE; Pico/eSpeak-ветки требуют их заголовки
CFLAGS = -std=gnu11 -D_GNU_SOURCE -Istub -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
STRICT = -Werror=implicit-function-declaration -Werror=int-conversion -Werror=incompatible-pointer-types

check:
	gcc -fsyntax-only $(STRICT) $(CFLAGS) ../main.c

# HAVE_RHVOICE: заголовки RHVoice лежат в stub (LGPL, копия src/include из RHVoice)
check-all: check
	gcc -fsyntax-only $(STRICT) $(CFLAGS) -DHAVE_SAM ../main.c
	gcc -fsyntax-only $(STRICT) $(CFLAGS) -DHAVE_SAM -DHAVE_RHVOICE ../main.c

# превью экрана с настоящими PNG и шрифтами: make preview  ->  /tmp/shots/*.png
preview: preview.c ../main.c $(wildcard stub/*.h stub/*/*.h)
	gcc -std=gnu11 -D_GNU_SOURCE -Istub $(shell pkg-config --cflags freetype2) -g -O1 -w preview.c -o preview.bin -lm -lpng $(shell pkg-config --libs freetype2) -Wl,--unresolved-symbols=ignore-all
	python3 shot.py
