# MyGarden — firmware ESP32

Firmware C++ para o protótipo com **um sensor capacitivo de solo e uma bomba**, preparado para a futura integração do aplicativo Flutter pela rede Wi-Fi local. O aplicativo continua em simulação; a conexão ao hardware será feita após montagem e validação em bancada.

O firmware não foi gravado em uma placa nem testado com água, sensor, relé ou bomba físicos. Compilação e testes de software, quando executados, não confirmam a pinagem do DevKit ou a compatibilidade elétrica dos módulos.

## Funções do firmware

- **Leitura de umidade:** recebe o sinal do sensor no GPIO34, filtra as amostras e calcula uma porcentagem relativa após a calibração.
- **Calibração persistente:** salva as referências seca e úmida em NVS, preservando-as ao desligar o ESP32.
- **Comunicação Wi-Fi:** oferece API HTTP local para consultar leituras, estado, calibração e comandos de irrigação.
- **Controle da bomba:** aciona o relé pelo GPIO26 em pulsos limitados, com armamento explícito, intervalo e orçamento de tempo de funcionamento.
- **Parada local:** interrompe o comando por prazo, alvo de umidade, leitura inválida ou antiga, perda de rede, expiração do armamento ou pedido de parada.
- **Validação dos comandos:** exige autenticação e vínculo à única planta configurada; identifica comandos repetidos para não acionar a bomba novamente.
- **Consulta de eventos:** informa o estado da execução, a duração observada e o motivo da parada para o futuro histórico do app. Os oito comandos recentes ficam em memória, sem histórico permanente no ESP32.
- **Irrigação manual ou por intenção automática:** avalia os limites enviados pelo cliente; comandos automáticos exigem habilitação na configuração. Não há irrigação autônoma com o app fechado.

A bomba vem desabilitada por padrão. A temperatura é informada como ausente, pois esse circuito não possui sensor de temperatura. O protocolo está preparado para a integração futura; o app ainda funciona em simulação.

## Circuito e escopo

| Componente / função | Ligação ao ESP32 |
|---|---|
| Sensor capacitivo v1.2, AOUT | GPIO34, entrada ADC |
| Relé de um canal, IN | GPIO26, saída digital após confirmar o nível ativo |
| Alimentação do sensor e do módulo relé | Barramento de 3,3 V do DevKit |
| Referência do sensor e do módulo relé | GND do DevKit |
| Alimentação do ESP32 | USB independente |
| Potência da bomba | Fonte externa 5 V / 2 A → COM → NO → bomba |

O negativo da bomba retorna ao negativo da fonte externa. O diodo 1N4007 fica em paralelo com a bomba, com **cátodo/faixa no positivo** e ânodo no negativo. NC fica sem fio. O circuito de controle não recebe os 5 V externos.

`esp32dev` é o alvo genérico de compilação. **ESP32-WROOM-32 identifica o módulo, não a posição dos pinos de um DevKit específico.** Confira a serigrafia de GPIO34, GPIO26, 3V3 e GND na placa real. O relé precisa aceitar alimentação VCC de 3,3 V, além de lógica de entrada de 3,3 V.

Não há sensor de temperatura neste circuito: a API fornece `temperature: null`. O firmware não inventa temperatura nem incrementos de umidade após irrigar.

## Organização

```text
include/config.h             Pinos, limites e opções de funcionamento
include/secrets.example.h    Modelo das configurações locais
include/secrets.h            Wi-Fi, token e vínculo da planta; ignorado pelo Git
src/main.cpp                 GPIO, Wi-Fi, API HTTP e tarefa de controle
lib/GardenSafety/            Política de controle C++ independente do hardware
test/host/                   Testes locais da política de controle
tools/test_host.py           Compila e executa os testes locais
platformio.ini              Ambiente Arduino/ESP32
API.md                      Protocolo HTTP e estados
INTEGRACAO_APP.md            Mapeamento para o app e integração pendente
```

## Configuração inicial

Copie `include/secrets.example.h` para `include/secrets.h`. Configure as macros seguindo o modelo e os locais de definição indicados em `config.h`:

| Macro | Uso |
|---|---|
| `MYGARDEN_WIFI_SSID` | Nome da rede Wi-Fi de 2,4 GHz |
| `MYGARDEN_WIFI_PASSWORD` | Senha da rede |
| `MYGARDEN_API_TOKEN` | Token aleatório local de 32 a 128 caracteres |
| `MYGARDEN_PLANT_ID` | ID exato do único cadastro associado a este circuito, de 1 a 64 caracteres |
| `MYGARDEN_RELAY_ACTIVE_LEVEL` | `-1` até confirmar; depois `0` para ativo em LOW ou `1` para ativo em HIGH |
| `MYGARDEN_PUMP_ENABLED` | `0` inicialmente; `1` apenas no comissionamento da bomba |
| `MYGARDEN_AUTOMATIC_ENABLED` | `0` inicialmente; liberação explícita de intenções automáticas do app |
| `MYGARDEN_WEB_ORIGIN` | Origem web exata autorizada, opcional; não usar `*` |

Não publique `secrets.h`, senha de Wi-Fi ou token. Não coloque token em URL ou parâmetros de consulta.

Com nível ativo `-1`, o GPIO do relé permanece como entrada e o firmware não habilita acionamentos. Isso **não garante o estado elétrico de um módulo conectado a uma entrada flutuante**. Faça a primeira configuração com a alimentação de 5 V da bomba desligada e confirme o comportamento durante inicialização, reset e gravação.

O ESP32 atua como estação Wi-Fi (STA); não cria ponto de acesso aberto. Todos os endpoints da aplicação exigem `Authorization: Bearer <token>`. HTTP destina-se somente à rede local de confiança: não há TLS nem proteção do token contra observação nessa rede. Não exponha essa porta na internet.

`WebServer` atende um cliente por vez. Clientes lentos ou corpos grandes podem atrasar a API, inclusive `/irrigation/stop`. O limite lógico de 1 KiB é verificado depois de receber o corpo e não protege previamente a alocação de memória do transporte. A tarefa local que verifica prazo e intertravamentos é independente do atendimento HTTP; isso não transforma o protótipo em serviço para rede pública.

## Compilar

O ambiente fixa `platformio/espressif32@8.0.0`, framework Arduino, placa `esp32dev` e `ArduinoJson@7.4.2` em `platformio.ini`. **Compilação confirmada em 20/11/2026**, com Arduino-ESP32 3.3.12 e configuração padrão sem credenciais/acionamento. O PlatformIO concluiu com `SUCCESS`: RAM estática de 51.244 bytes (15,6%) e programa de 981.758 bytes (74,9% da partição disponível). Os números não incluem o consumo dinâmico durante operação.

Com PlatformIO instalado, execute dentro de `firmware/`:

```bash
pio run
```

Neste workspace, as ferramentas podem estar instaladas localmente em `.tools/pio`, com os dados em `.pio-core`. No PowerShell, essa instalação pode ser usada sem instalação global:

```powershell
$env:PLATFORMIO_CORE_DIR = Join-Path (Get-Location) '.pio-core'
& '.\.tools\pio\Scripts\pio.exe' run
```

`.tools/`, `.pio-core/`, `.pio/` e segredos locais não devem entrar no Git.

## Testes locais confirmados

Dentro de `firmware/`, com Python e um compilador C++ (`g++` ou `clang++`):

```bash
python tools/test_host.py
```

Se o compilador não estiver no PATH ou nos locais reconhecidos pelo script, configure `MYGARDEN_CXX` com o caminho para seu executável. O script compila com C++11, `-Wall -Wextra -Werror -pedantic` e grava o executável em `build/host/`.

Resultado confirmado: **271 verificações da política C++11 passaram no computador**. São testes da calibração, estados, intertravamentos, prazos, limites e repetição de comandos; não executam `src/main.cpp`, Wi-Fi, HTTP, NVS ou GPIO de um ESP32. A compilação embarcada foi verificada separadamente; os testes físicos continuam pendentes.

## Primeira montagem e gravação — etapa futura

Nenhum comando de gravação abaixo foi executado como parte da preparação do código.

1. **Desligue a fonte de 5 V da bomba.** Confira as 13 ligações, polaridade do diodo, continuidade dos barramentos e pinagem real. Confirme VCC de 3,3 V do relé e a capacidade do regulador do DevKit para sensor e módulo.
2. Com a potência ainda desligada, determine o nível ativo de IN pela documentação e teste do módulo. Ajuste `MYGARDEN_RELAY_ACTIVE_LEVEL`; mantenha `MYGARDEN_PUMP_ENABLED=0` e `MYGARDEN_AUTOMATIC_ENABLED=0`.
3. Conecte o ESP32 por USB e confirme placa/porta. Somente então grave e abra o serial:

   ```bash
   pio run --target upload
   pio device monitor --baud 115200
   ```

4. Descubra o IP no serial. Na mesma rede, consulte `GET /api/v1/status` e `GET /api/v1/reading`, com token. Confirme identidade, `bootId`, calibração e ausência de acionamento.
5. **Meça a calibração do sensor real.** Observe o ADC bruto numa referência seca (`dryRaw`) e numa referência úmida (`wetRaw`) consistente com o uso pretendido. Não molhe a eletrônica da sonda. Não copie valores de outro sensor. Envie `PUT /api/v1/calibration` com as medidas, `bootId` atual e `plantId`. A amplitude absoluta deve ser de pelo menos 200 contagens, no ADC de 12 bits. A configuração é salva em NVS.
6. A porcentagem é relativa a essas referências, não uma medição certificada de teor volumétrico de água. Os limites demonstrativos 35%/65% do app não são recomendações agronômicas.
7. Após confirmar repouso/reset, corrente de partida da bomba, contatos DC e diodo, altere `MYGARDEN_PUMP_ENABLED=1` e grave novamente. O reset muda `bootId` e desfaz o armamento; a calibração válida permanece salva.
8. Prepare bomba e reservatório conforme o fabricante e ligue a fonte. Leia status, arme explicitamente com `POST /api/v1/arm` e solicite **um pulso curto supervisionado**, na faixa de 100–5000 ms. Consulte o resultado pela mesma `sequence` até concluir. Verifique também a parada explícita. Há intervalo de 60 segundos entre ciclos.
9. Verifique desconexão de rede, reinicialização, leitura inválida e expiração com o circuito supervisionado. Uma resposta HTTP não comprova o funcionamento hidráulico.
10. Faça a [integração do app](INTEGRACAO_APP.md). Só depois decida sobre intenções automáticas, com limites definidos para a planta e montagem reais.

## Controle e limites

- Bomba desabilitada inicialmente; nível ativo do relé não presumido.
- Armamento explícito por cinco minutos; reiniciar não restaura armamento.
- Uma planta fixa; comandos de outra planta ou de `bootId` antigo são rejeitados.
- Pulso de 100–5000 ms, sem funcionamento indefinido.
- Parada por tarefa independente do HTTP, com ciclo nominal de 10 ms.
- Parada em perda de rede, expiração, sensor inválido ou desatualizado por mais de três segundos, alvo ou fim do tempo.
- Intervalo de 60 segundos após cada ciclo; limite de 30 segundos acumulados em janela móvel de uma hora.
- Parada explícita desarma. O evento registra a duração observada pelo controle em milissegundos, que pode ser menor que o pedido por interrupção antecipada. O ciclo de 10 ms é nominal, não uma garantia de tempo real: agendamento ou falha do sistema pode atrasar a parada.
- `automatic` avalia uma intenção enviada pelo app; **não há irrigação autônoma com o app fechado**.

Não há sensor de fluxo, nível do reservatório ou realimentação do relé: o evento confirma o comando elétrico, não água entregue. Uma leitura analógica plausível não comprova sonda íntegra ou bem posicionada. Entrada flutuante, contato colado e falhas elétricas não são resolvidos por limites de software.

Calibração é persistente; cache de eventos, sequências, armamento, intervalo e orçamento de tempo ficam em RAM e são reiniciados com o ESP32. O limite de uma hora não é preservado entre reinicializações. Não use reset como maneira de contornar descanso ou limite da bomba.

Veja [API.md](API.md) para autenticação, sequências, respostas e limitações de horário.

## Referências de implementação

- [Espressif — ADC no Arduino-ESP32](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/adc.html): leitura bruta, resolução e leitura em milivolts.
- [Espressif — Preferences](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/preferences.html): armazenamento da calibração em NVS.
- [PlatformIO — ESP32 Dev Module](https://docs.platformio.org/en/latest/boards/espressif32/esp32dev.html): alvo de compilação `esp32dev`.
