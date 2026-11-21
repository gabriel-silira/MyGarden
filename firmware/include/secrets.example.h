#pragma once

// Copie para secrets.h (ignorado pelo Git). Nao use suas senhas neste exemplo.
#define MYGARDEN_WIFI_SSID ""
#define MYGARDEN_WIFI_PASSWORD ""
// Gere um token aleatorio de pelo menos 32 caracteres e guarde-o para o app.
#define MYGARDEN_API_TOKEN ""
// ID do cadastro no app, nao o nome visivel. Uma planta por circuito.
// 'mint' e apenas o ID inicial da hortela demonstrativa; confirme o seu cadastro.
#define MYGARDEN_PLANT_ID ""

// Primeiro teste com a fonte de 5 V da BOMBA DESLIGADA.
// -1: desconhecido (GPIO26 em entrada). 0: rele ativo LOW. 1: ativo HIGH.
// Confirme o modulo real; nivel incorreto pode ligar a bomba involuntariamente.
#define MYGARDEN_RELAY_ACTIVE_LEVEL -1
#define MYGARDEN_PUMP_ENABLED 0
#define MYGARDEN_AUTOMATIC_ENABLED 0

// Origem web exata, opcional, sem barra final, por exemplo http://localhost:5000.
// Vazio: acesso por navegador bloqueado. Android/cliente nativo nao usa Origin.
#define MYGARDEN_WEB_ORIGIN ""
