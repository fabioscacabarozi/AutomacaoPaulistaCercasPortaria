# Automação Paulista — Cercas & Portaria

Firmware para ESP32 que monitora e controla até 4 cercas elétricas via interface web em tempo real. As cercas 1 e 2 são gerenciadas diretamente pelo ESP32; as cercas 3 e 4 são delegadas a uma API Java externa.

## Visão geral

```
Sensor de disparo ──► ESP32 ──► Relé ──► Cerca elétrica
                         │
                    Interface Web (porta 80)
                         │
                    API Java (cercas 3 e 4)
                    http://192.168.0.107:8080
```

- Detecção de disparo por borda de subida nas entradas digitais
- Acionamento/desligamento de relés via painel web ou API REST
- Autenticação por `X-API-Key` em todos os endpoints
- Alarme sonoro no navegador quando uma cerca é disparada
- Reconexão automática ao WiFi em caso de queda

## Hardware

### Pinagem

| Função   | Pino ESP32 |
|----------|-----------|
| Relé 1   | GPIO 23   |
| Relé 2   | GPIO 5    |
| Relé 3   | GPIO 4    |
| Relé 4   | GPIO 13   |
| Entrada 1| GPIO 25   |
| Entrada 2| GPIO 26   |
| Entrada 3| GPIO 27   |
| Entrada 4| GPIO 33   |

As entradas usam `INPUT_PULLUP` com lógica invertida (LOW = acionado). Os relés são acionados em LOW.

## Configuração

Antes de compilar, edite as constantes em [src/main.cpp](src/main.cpp):

```cpp
#define WIFI_SSID   "sua_rede"
#define WIFI_PASS   "sua_senha"
#define API_KEY     "sua_chave_secreta"

// IP estático atribuído ao ESP32
WiFi.config(IPAddress(192, 168, 0, 47), ...);

// Endereço da API Java (cercas 3 e 4)
#define EXT_STATUS_URL  "http://192.168.0.107:8080/status?id=%d"
#define EXT_TOGGLE_URL  "http://192.168.0.107:8080/ligardesligar/%d"
```

## Build e flash

Projeto configurado com **PlatformIO** para a placa `esp32dev`.

```bash
# Compilar
pio run

# Gravar no ESP32
pio run --target upload

# Monitor serial (115200 baud)
pio device monitor
```

## API REST

Todos os endpoints exigem o header `X-API-Key: <chave>`.

| Método | Endpoint      | Descrição                                 |
|--------|---------------|-------------------------------------------|
| GET    | `/`           | Interface web                             |
| GET    | `/status/N`   | JSON com estado da cerca N (1–4)          |
| POST   | `/fence/N`    | Ativa ou desativa a cerca N               |

### Exemplo de resposta — `/status/1`

```json
{ "enabled": true, "triggered": false }
```

### Exemplo de requisição — `/fence/1`

```bash
curl -X POST http://192.168.0.47/fence/1 \
  -H "X-API-Key: sua_chave_secreta" \
  -H "Content-Type: application/json" \
  -d '{"enabled": true}'
```

## Interface web

Acesse `http://192.168.0.47` no navegador da rede local. O painel atualiza o estado das cercas a cada 2 segundos e emite um alarme sonoro enquanto houver disparo ativo. O alarme pode ser silenciado pelo botão na tela.

## Arquitetura de cercas

| Cerca | Controle         | Entrada | Relé |
|-------|-----------------|---------|------|
| 1     | ESP32 (local)   | GPIO 25 | GPIO 23 |
| 2     | ESP32 (local)   | GPIO 26 | GPIO 5  |
| 3     | API Java (proxy)| —       | GPIO 4  |
| 4     | API Java (proxy)| —       | GPIO 13 |

As cercas 3 e 4 têm o estado de `enabled` e `triggered` gerenciado pela API Java. O ESP32 age como proxy: consulta a API no `/status?id=N` e delega o toggle para `/ligardesligar/N`.
