# MyGarden — Aplicativo

Aplicativo desenvolvido em Flutter para acompanhar plantas e organizar os cuidados com um jardim. Projeto de uso pessoal, com foco em Android.

## Funcionalidades atuais

- Cadastro, edição e remoção de plantas.
- Escolha e alteração do ícone da planta entre hortelã, cidreira e cebolinha.
- Visão geral do jardim com umidade do solo e temperatura simuladas.
- Monitoramento com gráficos e histórico local persistente por planta, com filtros de período e opção Tudo.
- Botão para apagar todo o histórico da planta selecionada, com confirmação e preservação do cadastro.
- Irrigação manual e automática em modo de simulação, com limites configuráveis por planta.
- Configuração do nome do jardim e exibição da temperatura.
- Temas claro, escuro e do sistema, com ícones e tela de carregamento.
- Salvamento local dos cadastros e preferências.

**A versão atual funciona inteiramente em simulação.** Ainda não recebe dados de sensores nem aciona uma bomba real. A irrigação automática é avaliada ao atualizar as leituras; não há monitoramento em segundo plano. Leituras e irrigações são gravadas localmente e recuperadas ao reabrir o aplicativo. As 24 leituras demonstrativas iniciais não são recriadas a cada abertura.

O histórico fica em uma base local separada das preferências (arquivo no Android e IndexedDB no navegador), sem limite automático de 500 leituras ou 200 irrigações. Excluir uma planta remove seus registros; excluir dados/desinstalar o app pode apagá-los. Não há backup ou sincronização em nuvem. Na web, cada navegador e endereço possui seus próprios registros. Falhas ao salvar são indicadas no app e os registros pendentes são tentados novamente ao atualizar, enquanto a sessão permanece aberta.

## Integração futura com Arduino

O [firmware C++ do ESP32](../firmware/README.md) foi preparado para o protótipo com um sensor de solo e uma bomba, incluindo API HTTP local, calibração e limites de acionamento. O aplicativo continua em simulação e ainda não envia comandos a esse firmware. Após a montagem, será necessário implementar o adaptador de comunicação, vincular uma planta ao dispositivo e validar o circuito em bancada. Consulte o [protocolo](../firmware/API.md) e o [plano de integração](../firmware/INTEGRACAO_APP.md). Nenhum hardware foi gravado ou acionado nesta etapa.

## Executar o projeto

Execute os comandos nesta pasta (`app/`), com o Flutter instalado e um dispositivo Android conectado:

```bash
flutter pub get
flutter run
```

Para executar os testes:

```bash
flutter test
```

O projeto também possui suporte web para testes da interface no computador.

Para testar a persistência em um Android de testes, execute as duas fases em ordem:

```bash
flutter test --no-uninstall integration_test/history_persistence_android_test.dart --dart-define=historyValidationPhase=write
flutter test --no-uninstall integration_test/history_persistence_android_test.dart --dart-define=historyValidationPhase=read
```

Use sempre `--no-uninstall`: sem essa opção, o Flutter desinstala o aplicativo ao finalizar cada teste, apagando também os dados locais. As fases usam registros próprios de validação. Depois, reinstale a versão normal como atualização, sem desinstalar o app.
