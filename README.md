# MyGarden

Projeto pessoal para acompanhar plantas e organizar os cuidados com um jardim, reunindo o aplicativo Flutter e o espaço para o futuro firmware Arduino/ESP32.

## Organização

```text
app/        Aplicativo Flutter para Android, com suporte web para testes
firmware/   Código do Arduino/ESP32, ainda não implementado
```

## O que funciona hoje

- Cadastro, edição e remoção de plantas.
- Escolha de ícone por planta: hortelã, cidreira ou cebolinha.
- Umidade do solo e temperatura simuladas, com gráficos e histórico salvo por planta.
- Exclusão manual do histórico de uma planta, com confirmação, mantendo seu cadastro.
- Irrigação manual e automática simuladas, com limites por planta.
- Temas claro, escuro e do sistema.
- Salvamento local dos cadastros e preferências.

**O aplicativo ainda funciona inteiramente em simulação.** Não recebe leituras de sensores físicos nem aciona uma bomba real. Leituras e irrigações ficam salvas neste dispositivo após fechar e reabrir o app; não há monitoramento em segundo plano.

O histórico não expira automaticamente. Excluir uma planta remove seus registros. Não há backup em nuvem: desinstalar o app ou limpar seus dados pode apagar o histórico. Na prévia web, os registros pertencem ao navegador e endereço usados, sem sincronização com o celular.

## Executar o aplicativo

Com Flutter instalado e um dispositivo Android conectado:

```bash
cd app
flutter pub get
flutter run
```

Os comandos Flutter devem ser executados dentro de `app/`. Consulte o [README do aplicativo](app/README.md) para mais detalhes.

## Integração futura

O objetivo é integrar o aplicativo a um sistema físico com Arduino/ESP32, sensores e irrigação. O protocolo de comunicação e o acionamento real ainda serão desenvolvidos e validados. A pasta [firmware/](firmware/README.md) está reservada para essa parte do projeto.
