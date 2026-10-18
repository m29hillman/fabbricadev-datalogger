# Fabbrica Dev Datalogger

Firmware para a placa [Waveshare ESP32-S3-RS485-CAN](https://www.waveshare.com/wiki/ESP32-S3-RS485-CAN) (SKU 32154). Lê um inversor Sungrow SG6K-D por Modbus RTU na RS485, mostra as medidas numa página web e publica o mesmo JSON num broker MQTT.

O módulo é um ESP32-S3R8: 16 MB de flash, 8 MB de PSRAM octal e console pela USB CDC.

## Ligação

| Placa | Inversor |
| --- | --- |
| A+ | A+ (ou D+) do par RS485 |
| B- | B- (ou D-) do mesmo par |

A UART usada é a 1, nos GPIO 17 (TX), 18 (RX) e 21 (DE, junto com /RE). DE em nível alto transmite. O padrão de fábrica é escravo 1, 9600 8N1.

Se não houver resposta, inverta A e B e feche o jumper de terminação de 120 Ω nesta ponta. Use o par de comunicação do inversor, não o par do medidor de energia. O inversor é escravo: fica em silêncio até receber uma consulta.

Este firmware só lê. Não envia funções de escrita (06 e 16).

## Primeiro uso

1. Grave o firmware (veja abaixo).
2. No celular ou no computador, entre na rede Wi-Fi aberta `fabbricadev-datalogger`.
3. Abra `http://192.168.4.1`. Usuário e senha iniciais: `admin` / `admin`.
4. Na aba Rede, salve o nome da placa e o Wi-Fi da casa. Na aba MQTT, se quiser, habilite o broker.
5. Troque a senha da página na aba Usuário.

Quando a placa entra na rede da casa, a rede aberta some cerca de 2 segundos depois. Se ficar um minuto sem conseguir conectar, a rede aberta volta com o nome da placa e a tentativa na rede da casa continua. O nome padrão é `fabbricadev-datalogger`. Com a placa conectada, a página também responde em `http://fabbricadev-datalogger.local`.

Cinco senhas erradas seguidas bloqueiam o login por 5 minutos. O comando `resetar-senha` no console volta o login para `admin` / `admin`. A opção Restaurar padrão na página também apaga a rede e o MQTT, devolve o nome `fabbricadev-datalogger` e o modelo Sungrow SG6K-D. A configuração fica na memória da placa (NVS).

## Página e MQTT

A aba Estatística mostra potência, energia, tensões e correntes lidas do mapa Sungrow (protocolo de string inverters). O mesmo conteúdo sai no MQTT, com a mensagem retida, no tópico `fabbricadev/inversor` se a URL não tiver caminho. O intervalo fica na aba MQTT e o padrão é 10 segundos.

Formato da URL:

```text
mqtt://usuario:senha@192.168.1.10:1883/fabbricadev/inversor
mqtts://usuario:senha@broker.exemplo:8883/fabbricadev/inversor
ws://usuario:senha@192.168.1.10:8083/mqtt/fabbricadev/inversor
wss://usuario:senha@broker.exemplo:443/mqtt/fabbricadev/inversor
```

`mqtt://` usa a porta 1883 e `mqtts://` a 8883. O caminho depois do host é o tópico. `ws://` usa a porta 80 e `wss://` a 443. Nessas duas, a primeira pasta é o caminho do WebSocket (em geral `mqtt`) e o restante é o tópico. Usuário e senha são opcionais.

Exemplo do JSON:

```json
{
  "modelo": "Sungrow SG6K-D",
  "serie": "A123...",
  "tipo": 1,
  "potencia_nominal_kw": 6.0,
  "energia_dia_kwh": 12.3,
  "energia_total_kwh": 4500,
  "temperatura_c": 42.1,
  "mppt1_tensao_v": 360.0,
  "mppt1_corrente_a": 4.2,
  "mppt2_tensao_v": 355.0,
  "mppt2_corrente_a": 4.0,
  "potencia_cc_w": 2800,
  "tensao_v": 220.5,
  "corrente_a": 12.1,
  "potencia_w": 2650,
  "fator_potencia": 0.990,
  "frequencia_hz": 60.0,
  "estado_codigo": 2,
  "estado": "..."
}
```

Campos ainda não lidos saem como `null`.

## Console

A USB (115200 8N1) e o Telnet na porta 23 mostram o quadro Modbus e aceitam os mesmos comandos. Dois clientes Telnet ao mesmo tempo.

| Comando | Efeito |
| --- | --- |
| `ajuda` | Lista os comandos |
| `status` | Rede, modelo, baud e contadores |
| `varredura` | Volta a consultar o inversor |
| `escuta` | Só mostra o que chegar na RS485 |
| `escravo <1-247>` | Endereço Modbus |
| `baud <1200\|2400\|4800\|9600\|19200\|38400\|115200>` | Velocidade da UART |
| `intervalo <ms>` | Pausa entre consultas (padrão 2500 ms) |
| `ler <3\|4> <end> <qtd>` | Leitura pelo endereço do manual (o quadro usa endereço − 1) |
| `lerraw <3\|4> <end> <qtd>` | Leitura pelo endereço exato do PDU |
| `enviar <hex>` | Envia os bytes como estão |
| `enviarcrc <hex>` | Acrescenta o CRC16 Modbus |
| `mapa` | Referência usada na decodificação |
| `resetar-senha` | Login da página volta para `admin` / `admin` |

O modo inicial é varredura: tipo do equipamento, blocos 5000–5038 e número de série.

## Compilar

Requer [PlatformIO](https://platformio.org/).

```bash
pio run -e esp32-s3-rs485-can
pio run -e esp32-s3-rs485-can -t upload
pio device monitor -b 115200
```

A rede da casa pode ser copiada na primeira gravação, antes de existir configuração na página. Copie o exemplo e preencha:

```bash
cp include/secrets.example.h include/secrets.h
```

`include/secrets.h` não entra no Git. Depois que a página salva a rede, ela passa a mandar: o arquivo só vale na primeira inicialização.
