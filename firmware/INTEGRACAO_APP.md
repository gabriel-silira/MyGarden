# Integração futura com o aplicativo

O firmware foi preparado para o circuito MyGarden, mas **o aplicativo permanece em simulação**. Montagem, validação em bancada e transporte Flutter devem preceder comandos físicos na interface. Nenhum arquivo Dart foi alterado nesta preparação do firmware.

## Base existente

| Arquivo relativo à raiz | Recurso |
|---|---|
| `app/lib/data/garden_repository.dart` | Contrato `GardenRepository`: fonte, conexão, leitura, histórico e irrigação |
| `app/lib/domain/models.dart` | `DataSource.device`, `Plant.deviceId`, temperatura opcional e eventos com duração |
| `app/lib/data/history_store.dart` | Persistência e deduplicação |
| `app/lib/state/garden_controller.dart` | Coordenação da interface e dos dados |
| `app/lib/main.dart` | Instancia exclusivamente o simulador |

Veja [API.md](API.md) para o protocolo de rede.

## Uma planta por dispositivo

O app permite vários cadastros; a montagem tem um sensor, uma bomba e um relé. Escolha **qual cadastro** corresponde à planta física e use seu ID em `MYGARDEN_PLANT_ID`.

Não atribua a mesma leitura a todas as plantas nem acione a bomba para qualquer cadastro selecionado. O firmware rejeita identidades divergentes; o adaptador deve validar `plantId` e `deviceId`. `sim-mint` identifica o simulador, não um ESP32 real.

`Plant.deviceId` existe, mas `copyWith` conserva seu valor antigo. Implementar associação explícita e persistente no modelo/controlador, sem apagar histórico ou vincular pelo nome da planta.

## Adaptador de transporte

Criar `Esp32GardenRepository` e cliente HTTP separado. Manter simulação para testes, com a fonte claramente identificada.

| Operação atual | Implementação física |
|---|---|
| `source` | `DataSource.device` |
| `connection` | Comunicação validada, com erro/desconexão explícitos |
| `history(plant)` | Lista vazia inicialmente, sem as 24 amostras demonstrativas |
| `read(plant, temperatureEnabled: ...)` | Consultar leitura e validar identidade, calibração, atualidade e envelope |
| `irrigate(plant, mode: ...)` | Enviar, acompanhar a mesma sequência e retornar evento somente ao obter resultado final |

`GardenRepository` ainda não expõe armamento, calibração, parada, duração de pulso, consulta de execução ou limites. Acrescentar um contrato específico de controle físico ou estender a aplicação explicitamente. Não ocultar armamento/acionamento dentro de leitura ou abertura de tela.

Guardar token/configuração localmente, fora do código versionado, logs e URLs. Para múltiplos clientes, coordenar a sequência com `lastAcceptedSequence` do status: duas instâncias não podem presumir o mesmo próximo número. Ao reabrir o app, não reiniciar em 1 se `bootId` continua igual.

## Leituras, horário e histórico

- Usar `source: "device"`; falha não vira leitura simulada.
- Temperatura permanece `null`; o ajuste “exibir temperatura” não cria sensor físico.
- Leitura sem calibração, inválida ou antiga deve indicar indisponibilidade e bloquear irrigação.
- `timestamp` pode ser `null` antes do SNTP. Os modelos atuais exigem `DateTime`; não chamar seus `fromJson()` sem tratar isso.
- Definir origem do horário no modelo e preservar `timestampSource`. Se o cliente estimar horário por `sample.ageMs`, identificá-lo como estimado e reconhecer amostras repetidas por `bootId` e `sample.sampledAtUptimeMs`, tratando rollover em sessões longas.
- Consultar a mesma amostra não deve gerar duplicatas por variações do relógio do cliente; o `readingKey` atual usa o JSON completo.
- Histórico é organizado por `plantId` e pode conter dados demonstrativos. Preservar registros e identificar/filtrar `simulated` versus `device`; não reclassificar dados antigos como reais.

## Irrigação e estado real

`GardenController.canIrrigate` só permite simulador. A automação também é restrita à simulação, durante atualização. A integração precisa verificar vínculo, prontidão, calibração, armamento, intervalo, duração e limites do firmware.

O controlador atual coloca `pump=idle` ao terminar uma operação, inclusive após erro. Para hardware, **timeout não comprova desligamento**. A UI deve mostrar estado desconhecido/desconectado se não puder confirmar, consultar a mesma sequência e permitir parada. Não liberar novo pulso porque o primeiro HTTP expirou.

Fluxo manual:

1. Consultar status; verificar identidade, `bootId`, sensor e limites.
2. Solicitar armamento por ação explícita quando necessário.
3. Criar/guardar sequência e corpo para a intenção; enviar `/irrigation`.
4. Em `202`, acompanhar `/irrigation?sequence=N`: aceitação não é conclusão.
5. Em timeout, reconciliar a mesma sequência, sem criar outra para a mesma intenção.
6. Registrar evento final uma vez, com ID/duração observada e tratamento do horário; preservar `stopReason` e distinguir fim do prazo de interrupção por falha. Atualizar a leitura real sem incremento artificial de umidade.

Intenções `automatic` exigem opção de compilação habilitada. Não há automação com app fechado. Não copiar a simulação de “alcançar instantaneamente o alvo”: a água precisa distribuir-se no solo. Os limites 35/65 atuais são demonstração, não recomendação agronômica.

`pump.state`, `command.state` e `pump.feedbackAvailable: false` descrevem o comando do firmware, sem realimentação elétrica/hidráulica. Não apresentar o evento como prova de água entregue. Cache, sequências, intervalo e orçamento reiniciam junto com o ESP32: mostrar a mudança de `bootId`, desfazer expectativas da sessão anterior e nunca repetir automaticamente o pulso anterior.

## Interface e rede

Revisar textos fixos de simulação e indicadores nas telas de irrigação, monitoramento, histórico, plantas, visão geral, configurações, shell e widgets de eventos/gráficos. A origem acompanha cada registro. Não apresentar volume irrigado, sucesso hidráulico ou temperatura que o circuito não mede.

O manifesto Android principal ainda não declara `INTERNET`; a permissão existe no debug. Configurar a política de HTTP local/cleartext e testar um APK no dispositivo real, sem presumir que debug representa distribuição.

Na web, a origem deve corresponder exatamente a `MYGARDEN_WEB_ORIGIN`, incluindo host, esquema e porta. CORS não resolve mixed content (página HTTPS acessando ESP32 HTTP) nem todas as restrições de rede local do navegador. A rede deve permitir comunicação entre clientes; uma mudança de IP não pode vincular silenciosamente outro ESP32 ao cadastro.

## Critérios de conclusão futura

- [ ] Pinagem real, nível ativo e repouso do relé verificados.
- [ ] Calibração física e tratamento de indisponibilidade implementados.
- [ ] Uma planta vinculada e identidades validadas.
- [ ] Transporte HTTP, armamento e parada explícitos implementados.
- [ ] Repetição, timeout, reset e saída do cache não geram novo acionamento.
- [ ] Estado da bomba e conclusão só são afirmados com confirmação correspondente.
- [ ] Eventos salvos uma vez, com duração real e tratamento de horário não sincronizado.
- [ ] Histórico anterior preservado e origem real/simulada identificável.
- [ ] Temperatura não medida permanece ausente.
- [ ] Permissões/rede Android e teste em dispositivo físico concluídos.
- [ ] Perda de rede, falha do sensor, alvo, prazo, expiração e parada testados em bancada supervisionada.

Esta lista descreve trabalho pendente, não evidencia que integração física ou testes já ocorreram.
