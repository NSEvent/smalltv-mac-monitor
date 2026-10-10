OUT := macmetrics
SRC := src/main.swift src/smc.c

.PHONY: all clean

all: $(OUT)

$(OUT): $(SRC) src/smc.h
	swiftc -O -import-objc-header src/smc.h \
		-framework IOKit -framework CoreFoundation -framework Foundation \
		$(SRC) -o $(OUT)

clean:
	rm -f $(OUT)
