#include "inverter_model.h"

#include "settings.h"

#include <cstring>

namespace {

const InverterModel kModels[] = {
    {"sungrow-sg6k-d", "Sungrow SG6K-D"},
};

}  // namespace

const InverterModel *inverter_model_list(size_t *count) {
  if (count != nullptr) {
    *count = sizeof(kModels) / sizeof(kModels[0]);
  }
  return kModels;
}

const InverterModel *inverter_model_find(const char *id) {
  if (id == nullptr) {
    return nullptr;
  }
  for (const InverterModel &model : kModels) {
    if (strcmp(model.id, id) == 0) {
      return &model;
    }
  }
  return nullptr;
}

const char *inverter_model_name() {
  const InverterModel *model = inverter_model_find(settings_get().modelo);
  if (model == nullptr) {
    return kModels[0].nome;
  }
  return model->nome;
}

bool inverter_model_is_sungrow() {
  return strcmp(settings_get().modelo, "sungrow-sg6k-d") == 0;
}
