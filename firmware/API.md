# MyGarden — protocolo local HTTP v1

Contrato para o ESP32 e o futuro adaptador Flutter. Base: `http://<ip-do-esp32>/api/v1`. O aplicativo atual não chama essa API; continua usando o simulador.

## Identidade, rede e autenticação

- Cada dispositivo controla uma única bomba/sensor e uma única `plantId` configurada.
- `deviceId` identifica o ESP32 a partir da identidade de hardware.
- `bootId` é aleatório e muda a cada inicialização. Comandos de uma inicialização anterior são rejeitados.
- Todos os endpoints da aplicação exigem `Authorization: Bearer <MYGARDEN_API_TOKEN>`. Token de 32 a 128 caracteres, sem aparecer em URLs. `plantId` é uma string configurada com 1 a 64 caracteres.
- Escritas usam `Content-Type: application/json`, corpo não vazio e nenhum parâmetro de query na URL; o handler rejeita corpos acima de 1024 bytes. GET não leva corpo.
- Sem TLS: usar somente LAN de confiança, sem encaminhar a porta para internet.
- CORS opcional aceita só `MYGARDEN_WEB_ORIGIN` exatamente igual à origem do navegador. Host, esquema e porta fazem parte da origem. Android nativo não depende de CORS.
- Preflight não autoriza controle: a requisição real continua exigindo token e validação de identidade.

Leia o status ao conectar e após reset. Não reutilize `bootId` armazenado sem confirmar que continua atual.

O `WebServer` é síncrono e atende um cliente por vez. Clientes lentos ou grandes podem atrasar respostas, inclusive a parada remota. A verificação de 1024 bytes ocorre **após receber o corpo**; não limita antecipadamente a alocação de memória da biblioteca nem é defesa de transporte. A tarefa de parada local continua independente do HTTP. Use somente a LAN de confiança.

## Endpoints

| Método e caminho | Operação |
|---|---|
| `GET /status` | Estado do dispositivo, sensor, bomba, calibração e limites |
| `GET /reading` | Última leitura ou ausência explícita de leitura utilizável |
| `PUT /calibration` | Salvar referências do ADC em NVS |
| `POST /arm` | Autorizar comandos temporariamente, por cinco minutos |
| `POST /irrigation` | Solicitar pulso manual ou avaliação automática |
| `GET /irrigation?sequence=N` | Consultar execução sem acioná-la novamente |
| `POST /irrigation/stop` | Desligar e desarmar, de forma idempotente |

### Status e leitura

`GET /status` é a fonte de verdade para estado e prontidão. Sucesso HTTP não significa sensor calibrado ou bomba armada.

Respostas de sucesso incluem `apiVersion`, `firmwareVersion`, `deviceId`, `bootId`, `plantId`, `source`, `uptimeMs` e `clockSynchronized`. Além da leitura, o status inclui:

| Campo | Conteúdo |
|---|---|
| `connection`, `ip`, `hostname` | Estado Wi-Fi e endereço local |
| `lastAcceptedSequence` | Maior sequência aceita neste boot; não é contador persistente |
| `capabilities` | Solo disponível, temperatura ausente, uma planta, ausência de autonomia/histórico persistente e opção automática |
| `pump` | `state`, `hardwareEnabled`, `polarityConfirmed`, `feedbackAvailable`, `armed`, `armRemainingMs`, `cooldownRemainingMs`, `budgetUsedMs`, `budgetLimitMs`, `budgetWindowMs`, `maxPulseMs` |
| `calibration` | `confirmed`, `storageReady` e referências `dryRaw`/`wetRaw` quando válidas |
| `sample`, `reading` | Diagnóstico e leitura descritos abaixo |
| `command`, `event` | Último comando aceito e eventual evento final; ambos nulos antes do primeiro comando |

`pump.state` pode ser `unconfigured`, `idle` ou `running`. É estado do comando, não realimentação elétrica/hidráulica (`feedbackAvailable: false`). `clockSynchronized` descreve o relógio atual; cada leitura/evento mantém seu próprio `timestampSource`.

`GET /reading` devolve o envelope abaixo; identidades e valores são ilustrativos:

```json
{
  "apiVersion": 1,
  "firmwareVersion": "0.1.0",
  "deviceId": "identificador-do-esp32",
  "bootId": "boot-id-atual",
  "plantId": "id-do-cadastro-vinculado",
  "source": "device",
  "uptimeMs": 10000,
  "clockSynchronized": true,
  "sample": {
    "raw": 2362,
    "milliVolts": 1900,
    "rawSpread": 20,
    "valid": true,
    "calibrated": true,
    "ageMs": 120,
    "sampledAtUptimeMs": 9880
  },
  "reading": {
    "plantId": "id-do-cadastro-vinculado",
    "deviceId": "identificador-do-esp32",
    "moisture": 42.5,
    "temperature": null,
    "timestamp": "2026-11-18T15:00:00.000Z",
    "timestampSource": "device_sntp",
    "source": "device"
  }
}
```

Sem amostra utilizável, `reading` é `null` e `readingUnavailableReason` informa `sensor_invalid`, `sensor_stale` ou `not_calibrated`; `sample` continua disponível para diagnóstico e calibração. A leitura é amostrada nominalmente a cada 500 ms, usando média das oito amostras centrais de um conjunto de 16. A verificação rejeita extremos ADC próximos aos trilhos e dispersão excessiva; não detecta toda falha de sensor ou montagem.

Umidade é a escala relativa 0–100 entre `dryRaw` e `wetRaw`. Temperatura é sempre `null`, porque não há esse sensor no circuito. `sample.milliVolts` é uma conversão adicional do ADC, não uma medição de umidade nem uma tensão a aplicar ao pino.

UTC pode ser **`null` enquanto SNTP não estiver sincronizado**. O cliente deve verificar isso e a idade da amostra; não pode passar `null` a `SensorReading.fromJson`, que exige data válida. Uptime não é horário de calendário. Na integração, preservar a origem do horário e distinguir estimativa do cliente de horário do dispositivo.

`timestampSource` é `device_sntp` quando a amostra foi obtida com horário disponível, ou `unsynchronized` quando não foi. Use `bootId` e `sample.sampledAtUptimeMs` para reconhecer a mesma amostra dentro de uma inicialização; trate o rollover do contador de 32 bits em sessões longas. Uma sincronização posterior não preenche retroativamente timestamps antigos.

Leitura sem calibração, inválida ou desatualizada não deve virar `0%`, `100%` ou dado simulado. O adaptador deve representar indisponibilidade e impedir irrigação.

### Calibração

`PUT /calibration`:

```json
{
  "bootId": "boot-id-atual",
  "plantId": "id-do-cadastro-vinculado",
  "dryRaw": 3000,
  "wetRaw": 1500
}
```

**3000 e 1500 são somente exemplos de formato.** Substitua por medidas da própria sonda. Valores de 0–4095, diferença absoluta de pelo menos 200 contagens. A calibração é salva em NVS. Salvar calibração não autoriza ligar a bomba; armamento é separado.

Se a bomba estiver em execução, a calibração responde `409 busy`. Fora de execução, a tentativa desarma antes da escrita em NVS. A resposta bem-sucedida inclui `calibration.confirmed`, `dryRaw`, `wetRaw` e `armed: false`. Espere a próxima amostra antes de armar: a leitura anterior é invalidada para a nova calibração. Uma falha de armazenamento não deve ser apresentada como calibração salva.

### Armamento

`POST /arm`:

```json
{
  "bootId": "boot-id-atual",
  "plantId": "id-do-cadastro-vinculado"
}
```

Validade de cinco minutos. Apresentar o estado devolvido, sem inferi-lo pela intenção enviada. Não armar automaticamente ao ler sensor ou abrir tela. Reiniciar ou executar parada explícita exige novo armamento antes de outro acionamento.

A resposta `200` inclui `armed: true` e `armRemainingMs`. Armamento exige hardware habilitado, rede e amostra válida/calibrada/recente; não é aceito durante uma execução. Uma nova ação explícita de armar, em repouso, renova o prazo.

### Solicitação de irrigação

`POST /irrigation`:

```json
{
  "bootId": "boot-id-atual",
  "plantId": "id-do-cadastro-vinculado",
  "sequence": 1,
  "mode": "manual",
  "durationMs": 500,
  "minimum": 35,
  "target": 65
}
```

O exemplo mostra um pulso curto e limites demonstrativos; **não recomenda duração ou umidade para a planta real**.

| Campo | Regra |
|---|---|
| `bootId` | Inicialização atual |
| `plantId` | Única planta configurada |
| `sequence` | Inteiro sem sinal de 32 bits, maior que zero; crescente por `bootId` |
| `mode` | `manual` ou `automatic` |
| `durationMs` | Inteiro de 100 a 5000 ms |
| `minimum`, `target` | Escala 0–100, mínimo menor que alvo |

Configuração, calibração, armamento, rede, sensor, tempos e intervalo são verificados no dispositivo. Manual exige umidade abaixo do alvo. Automático exige `MYGARDEN_AUTOMATIC_ENABLED=1` e umidade abaixo do mínimo. A decisão pode ser negada mesmo se o app habilitou o botão.

- `202 Accepted`: comando aceito; **não significa conclusão**.
- `200 OK` para repetição reconhecida: estado do comando conhecido, sem novo acionamento.
- Erro: não converter em evento concluído; consultar estado se houver dúvida sobre uma tentativa anterior.

O firmware desliga no fim do pulso ou antes, por alvo, falha/intertravamento ou parada. Não existe comando “ligar até receber desligar”.

### Sequências e consulta

O dispositivo mantém `lastAcceptedSequence` por inicialização e cache dos oito comandos recentes. Uma nova ação exige sequência maior que a maior aceita (até 4294967295). Sequência antiga fora do cache é rejeitada, sem repetir acionamento; não reinicie o contador do cliente em 1 no mesmo boot. Esses dados ficam em RAM e se perdem após reset.

1. Crie sequência nova para uma nova intenção e guarde-a com `bootId` e corpo original.
2. Se o POST expirar ou a rede cair, **não gere outra sequência para a mesma intenção**. Consulte `GET /irrigation?sequence=N`.
3. Repetição deve manter identidade e conteúdo; não reaproveite sequência para outro pulso.
4. Se saiu do cache, não invente evento nem repita irrigação para “confirmar”. Apresente resultado indisponível e reconcilie estado.
5. Se `bootId` mudou, não reenvie a tentativa anterior sob a nova inicialização. Confirme o estado e exija nova intenção explícita.

A consulta, o POST aceito e o POST duplicado incluem `command` e `event`. `command` contém `sequence`, `state` (`running` ou `finished`), `mode`, `requestedDurationMs`, `actualDurationMs`, `startedAtUptimeMs` e `reason`. Enquanto executa, `event` é `null`. No POST, `duplicate` distingue aceitação nova de repetição reconhecida. Quando não há comando retido para a sequência consultada, GET responde `404 event_not_retained`.

O evento final tem esta estrutura:

```json
{
  "id": "identificador-unico-do-evento",
  "plantId": "id-do-cadastro-vinculado",
  "timestamp": "2026-11-18T15:00:00.500Z",
  "mode": "manual",
  "durationMicroseconds": 500000,
  "source": "device",
  "timestampSource": "device_sntp",
  "stopReason": "duration_elapsed"
}
```

`durationMicroseconds` é a duração observada pelo controle em milissegundos multiplicada por 1000; a unidade não implica resolução de microssegundos ou medição física. Pode terminar antes do pedido por intertravamento, ou ter atraso de agendamento. `timestamp` corresponde ao início registrado mais a duração observada; pode ser `null` se o relógio estava indisponível ao iniciar, mesmo que sincronize durante o pulso. O evento registra comando do relé, não volume de água entregue.

Motivos finais: `duration_elapsed`, `target_reached`, `manual_stop`, `disarmed`, `network_lost`, `sensor_invalid`, `sensor_stale`, `not_calibrated`, `arm_expired`. `none` indica ausência de motivo final durante execução. Não converter todo `finished` em sucesso hidráulico.

### Parada

`POST /irrigation/stop`:

```json
{
  "bootId": "boot-id-atual",
  "plantId": "id-do-cadastro-vinculado"
}
```

Desliga e desarma. Repetir não inicia operação. Timeout não comprova desligamento; confirme estado e use desligamento físico da alimentação quando necessário na bancada.

A resposta inclui `armed: false`, `pumpRunning: false` e o último `command`/`event`, se houver.

## Erros

Erros têm o formato `{"error":{"code":"codigo"},"bootId":"boot-id-atual"}`. Não há mensagem localizada no firmware; o adaptador deve traduzi-los sem descartar o código.

| HTTP | Códigos |
|---|---|
| 400 | `invalid_body`, `invalid_json`, `invalid_request`, `invalid_mode`, `invalid_sequence`, `invalid_calibration` |
| 401 | `unauthorized` |
| 403 | `origin_not_allowed` |
| 404 | `not_found`, `event_not_retained` |
| 409 | `boot_changed`, `plant_mismatch`, `conflict`, `stale_sequence`, `automatic_disabled`, `not_armed`, `sensor_invalid`, `sensor_stale`, `not_calibrated`, `busy`, `target_reached`, `not_dry` |
| 413 | `body_too_large` |
| 415 | `json_required` |
| 429 | `cooldown`, `budget_exceeded` |
| 503 | `device_not_configured`, `hardware_disabled`, `network_lost`, `calibration_storage_unavailable`, `calibration_save_failed` |

`conflict` significa sequência retida com corpo de comando diferente. `stale_sequence` significa sequência não retida que é menor ou igual à maior aceita. As verificações de autenticação/origem/JSON/identidade podem rejeitar a chamada antes da política da bomba.

Para `cooldown` e `budget_exceeded`, consulte o status; não há `Retry-After` nem estimativa de liberação do orçamento na resposta de erro. O orçamento considera integralmente cada pulso concluído dentro da última hora, mais tempo de execução atual, de maneira conservadora nos limites da janela. Intervalo, orçamento e eventos não sobrevivem a reset; calibração salva é a única configuração de controle persistida em NVS nesta versão.

## Semântica de integração

- Firmware decide prontidão e parada. Timer no celular não substitui o limite local.
- `automatic` é intenção avaliada quando enviada. Não há programação diária ou automação com app fechado.
- Cache de oito comandos não é histórico permanente. O app deve importar eventos localmente, com IDs estáveis e sem duplicação.
- Não há séries demonstrativas. `GardenRepository.history()` físico pode retornar lista vazia inicialmente.
- O adaptador deve preservar estados e metadados do envelope necessários para explicar indisponibilidade, horário e duração efetiva, sem fabricar campos para satisfazer modelos atuais.
