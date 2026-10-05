#pragma once

#include <stddef.h>
#include <stdint.h>

// Leitura de referencia do mapa Sungrow (protocolo de string inverters V1.1.37).
// O quadro cru continua sendo a fonte; estes rotulos podem nao bater no SG6K-D.
void sungrow_print_registers(uint16_t protocol_addr, const uint8_t *data, size_t len);
void sungrow_print_map();
