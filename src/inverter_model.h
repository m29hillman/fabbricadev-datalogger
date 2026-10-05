#pragma once

#include <stddef.h>

struct InverterModel {
  const char *id;
  const char *nome;
};

const InverterModel *inverter_model_list(size_t *count);
const InverterModel *inverter_model_find(const char *id);
const char *inverter_model_name();
bool inverter_model_is_sungrow();
