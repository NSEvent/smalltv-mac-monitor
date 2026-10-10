PIO := .venv/bin/pio
FW := firmware/.pio/build/smalltv/firmware.bin
LOADER := firmware/.pio/build/loader/firmware.bin
# Official Ultra-V9.0.50 app image is 505200 bytes. Stay under it for a direct OTA.
STOCK_SLOT := 500000

.PHONY: macmetrics firmware loader size install-macmetrics test

test:
	$(CXX) -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined tests/raster_test.cpp -o /tmp/smalltv-raster-test
	/tmp/smalltv-raster-test
	$(CXX) -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined tests/endpoints_test.cpp -o /tmp/smalltv-endpoints-test
	/tmp/smalltv-endpoints-test
	$(CXX) -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined tests/history_test.cpp -o /tmp/smalltv-history-test
	/tmp/smalltv-history-test
	$(CXX) -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -Itests/stubs tests/ui_test.cpp firmware/src/ui.cpp -o /tmp/smalltv-ui-test
	/tmp/smalltv-ui-test

macmetrics:
	$(MAKE) -C macmetrics

firmware:
	cd firmware && ../$(PIO) run -e smalltv
	@$(MAKE) size

loader:
	cd firmware && ../$(PIO) run -e loader
	@echo "loader bytes: $$(wc -c < $(LOADER))"

size:
	@test -f $(FW)
	@bytes=$$(wc -c < $(FW) | tr -d ' '); \
	echo "firmware.bin $$bytes bytes (stock OTA slot gate $(STOCK_SLOT))"; \
	test $$bytes -lt $(STOCK_SLOT)

install-macmetrics: macmetrics
	./scripts/install-macmetrics.sh
