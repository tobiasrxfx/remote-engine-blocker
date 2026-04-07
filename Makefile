# =============================================================================
#  REB — Remote Engine Blocker — Makefile
#  Alinhado ao REB-SRS-001 v0.2 | ISO 26262 | MISRA C | NFR-SW-001
# =============================================================================

CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Werror -pedantic \
          -Wno-unused-parameter \
          -I. \
          -I./include

# Fontes do core REB (backend — sua dupla)
CORE_SRCS = \
    src/reb_core/event_log.c      \
    src/reb_core/nvm.c            \
    src/reb_core/security_manager.c \
    src/reb_core/panel_auth.c     \
    src/reb_core/sensor_fusion.c  \
    src/reb_core/actuator_iface.c \
    src/reb_core/starter_control.c\
    src/reb_core/alert_manager.c  \
    src/reb_core/reversal_window.c\
    src/reb_core/fsm.c            \
    reb_main.c

# Objetos
CORE_OBJS = $(CORE_SRCS:.c=.o)

# --- Alvo principal: biblioteca estática (para integração com outras duplas) ---
.PHONY: all clean test lib

all: lib test

lib: libreb.a

libreb.a: $(CORE_OBJS)
	ar rcs $@ $^
	@echo ">>> libreb.a criada (integrar com dupla CAN e IHM)"

# --- Alvo de testes ---
TEST_SRCS = tests/test_reb.c
TEST_BIN  = test_reb

test: $(TEST_BIN)
	@echo "\n>>> Executando suite de testes REB..."
	./$(TEST_BIN)

$(TEST_BIN): $(CORE_SRCS) $(TEST_SRCS)
	$(CC) $(CFLAGS) -o $@ $^
	@echo ">>> Binario de testes compilado: $(TEST_BIN)"

# --- Compilação de objetos ---
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Limpeza ---
clean:
	rm -f $(CORE_OBJS) libreb.a $(TEST_BIN)
	@echo ">>> Limpeza concluida"

# --- Verificação MISRA C com cppcheck (NFR-SW-001) ---
misra:
	cppcheck --addon=misra --enable=all --error-exitcode=1 \
	         --suppress=missingIncludeSystem \
	         -I. -I./include \
	         $(CORE_SRCS)
	@echo ">>> Verificacao MISRA C concluida (0 violations = PASS)"

# --- Ajuda ---
help:
	@echo "Alvos disponiveis:"
	@echo "  all    — compila lib e roda testes"
	@echo "  lib    — gera libreb.a para integracao"
	@echo "  test   — compila e roda suite de testes"
	@echo "  misra  — verificacao MISRA C (requer cppcheck)"
	@echo "  clean  — remove artefatos de build"
