/*
 * Este projeto simula uma estacao meteorologica usando a Raspberry Pi Pico W.
 * Ele le dados de temperatura e umidade (AHT20) e pressao (BMP280), e calcula altitude.
 * Os dados sao mostrados em um servidor web, que oferece:
 * - Uma pagina inicial com os valores atuais.
 * - Graficos para cada grandeza medida.
 * - Uma pagina de configuracao para definir offsets e limites.
 * - Navegacao pela interface web usando botoes fisicos (A e B).
 * - Feedback visual (LEDs) e sonoro (buzzer) sobre o status da conexao Wi-Fi.
 */

#include <stdio.h>  // Para funcoes de entrada/saida.
#include <stdlib.h> // Para funcoes como atof (converter string para float).
#include <string.h> // Para manipulacao de strings.
#include <math.h>   // Para funcoes matematicas (ex: pow para calculo de altitude).

#include "pico/stdlib.h"     // Funcoes essenciais do Pico SDK.
#include "pico/cyw43_arch.h" // Biblioteca para arquitetura Wi-Fi da Pico com CYW43.
#include "hardware/i2c.h"    // Funcoes para controle do periferico I2C, usado para comunicacao com os sensores.
#include "hardware/gpio.h"   // Funcoes para controle dos pinos de entrada/saida (GPIO), usado para LEDs, buzzer e botoes, incluindo interrupcoes.
#include "hardware/clocks.h" // Clocks para o arquivo blink.pio.
#include "hardware/pio.h"    // PIO para a matriz de LEDs.

#include "lib/matriz.h" // Arquivo da pasta lib/ que contem o alerta que aparecera na matriz de LEDs.
#include "blink.pio.h"  // Arquivo em assembly para comunicacao com a matriz.

#include "lwip/tcp.h" // Funcoes para a pilha de rede TCP/IP, essencial para criar o servidor web.

#include "aht20.h"  // Arquivo para o sensor de temperatura e umidade AHT20.
#include "bmp280.h" // Arquivo para o sensor de pressao BMP280.

//-------------------------------------------Definicoes-------------------------------------------

// Credenciais da rede Wi-Fi
#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Definicao dos pinos
#define BUTTON_A 5
#define BUTTON_B 6
#define WS2812_PIN 7
#define LED_PIN_GREEN 11
#define LED_PIN_RED 13
#define BUZZER_PIN 21

// Configuracao do I2C
#define I2C_PORT_BMP280 i2c0
#define I2C_SDA_BMP 0
#define I2C_SCL_BMP 1
#define I2C_PORT_AHT20 i2c1
#define I2C_SDA_AHT 2
#define I2C_SCL_AHT 3

// Constantes fisicas
#define SEA_LEVEL_PRESSURE 101325.0 // Pressao ao nivel do mar em Pascal, para calculo de altitude.
#define MAX_CHART_POINTS 20         // Numero maximo de pontos a serem exibidos nos graficos.

#define DEBOUNCE_MS 500 // 500 ms para debounce dos botoes.

//-------------------------------------------Variaveis Globais-------------------------------------------

// Variaveis para navegacao pelos botoes
const char *G_PAGES[] = {"/", "/config", "/temperatura", "/umidade", "/pressao", "/altitude"}; // Paginas que serao usadas.
const int G_NUM_PAGES = sizeof(G_PAGES);                                                       // Calcula o numero total de paginas.
volatile int g_current_page_index = 0;                                                         // indice da pagina atual na lista G_PAGES.
volatile const char *g_target_page = NULL;                                                     // Ponteiro para a URL que o navegador deve carregar. Eh setado pela interrupcao do botao.
volatile uint32_t g_last_press_time = 0;                                                       // Armazena o tempo do ultimo clique para o debounce.

// Variaveis de configuracao (offsets, maximos e minimos)
volatile float g_temp_offset = 0.0f, g_temp_min = 10.0f, g_temp_max = 40.0f;
volatile float g_umid_offset = 0.0f, g_umid_min = 60.0f, g_umid_max = 85.0f;
volatile float g_press_offset = 0.0f, g_press_min = 85.0f, g_press_max = 105.0f;
volatile float g_alt_offset = 0.0f, g_alt_min = 800.0f, g_alt_max = 900.0f;

// Variaveis globais dos sensores
volatile float g_temperatura = 0.0f;
volatile float g_umidade = 0.0f;
volatile float g_pressao = 0.0f;
volatile float g_altitude = 0.0f;

// Definicao global do PIO
PIO pio;
uint sm;

//----------------------------------------Prototipos de funcoes---------------------------------------

void setup();
uint32_t matrix_rgb(double r, double g, double b);
void desenho_pio(double *desenho, PIO pio, uint sm, float r, float g, float b);
void gpio_callback(uint gpio, uint32_t events);
static void start_http_server();
float calculate_altitude(float pressure_pa);
static err_t send_chunk(struct tcp_pcb *tpcb, const char *data);
void send_full_response(struct tcp_pcb *tpcb, const char *content_template);
void send_json_response(struct tcp_pcb *tpcb, const char *payload);
void parse_post_data(const char *data);
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
void tocar_buzzer(uint freq, uint duracao);

//-------------------------------------------HTML-------------------------------------------

// Contem o cabecalho HTML, CSS para estilizacao e o script de navegacao por botao.
const char HTML_HEADER[] =
    "<!DOCTYPE html><html lang='pt-BR'><head><meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'>"
    "<title>Web Display</title>"
    "<link href='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/css/bootstrap.min.css' rel='stylesheet'>"
    "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"
    "<script src='https://cdn.jsdelivr.net/npm/chartjs-plugin-annotation@3.0.1/dist/chartjs-plugin-annotation.min.js'></script>"
    "<style>"
        "body { background-color: #f0f2f5; }"
        ".card p { font-size: 2.5rem; font-weight: 300; margin-bottom: 0; }"
        ".card .card-footer { font-size: 0.85rem; color: #6c757d; }"
        ".form-grid-item { display: flex; flex-direction: column; text-align: left; }"
    "</style>"
    "<script>"
        "function checkNavigation(){"
        "fetch('/navigate').then(r=>r.json()).then(d=>{"
        "if(d&&d.goto&&window.location.pathname!==d.goto){window.location.href=d.goto;}"
        "}).catch(e=>{});"
        "}"
        "setInterval(checkNavigation,1200);"
    "</script>"
    "</head><body class='text-center'>";

// Contem a barra de navegacao comum a todas as paginas.
const char HTML_NAV[] =
    "<nav class='navbar navbar-expand-lg navbar-light bg-white shadow-sm mb-4'>"
        "<div class='container-fluid'>"
            "<a class='navbar-brand' href='/'>Web Display</a>"
            "<button class='navbar-toggler' type='button' data-bs-toggle='collapse' data-bs-target='#navbarNav'>"
                "<span class='navbar-toggler-icon'></span>"
            "</button>"
            "<div class='collapse navbar-collapse' id='navbarNav'>"
                "<ul class='navbar-nav me-auto mb-2 mb-lg-0'>"
                    "<li class='nav-item'><a class='nav-link' href='/'>Início</a></li>"
                    "<li class='nav-item'><a class='nav-link' href='/config'>Configurações</a></li>"
                    "<li class='nav-item'><a class='nav-link' href='/temperatura'>Temperatura</a></li>"
                    "<li class='nav-item'><a class='nav-link' href='/umidade'>Umidade</a></li>"
                    "<li class='nav-item'><a class='nav-link' href='/pressao'>Pressão</a></li>"
                    "<li class='nav-item'><a class='nav-link' href='/altitude'>Altitude</a></li>"
                "</ul>"
            "</div>"
        "</div>"
    "</nav>";

// Contem o corpo da pagina inicial.
const char HTML_CONTENT_INICIO[] =
    "<main class='container'>"
        "<h1>Painel de Controle</h1>"
        "<div class='row g-4 justify-content-center mt-3' id='cards-container'>"
            "<div class='col-12 col-md-6 col-lg-3'><div class='card shadow-sm'><div class='card-body'><h2>Temperatura</h2><p><span id='temp_valor'>--</span> °C</p></div></div></div>"
            "<div class='col-12 col-md-6 col-lg-3'><div class='card shadow-sm'><div class='card-body'><h2>Umidade</h2><p><span id='umidade_valor'>--</span> %</p></div></div></div>"
            "<div class='col-12 col-md-6 col-lg-3'><div class='card shadow-sm'><div class='card-body'><h2>Pressão</h2><p><span id='pressao_valor'>--</span> kPa</p></div></div></div>"
            "<div class='col-12 col-md-6 col-lg-3'><div class='card shadow-sm'><div class='card-body'><h2>Altitude</h2><p><span id='alt_valor'>--</span> m</p></div></div></div>"
        "</div>"
    "</main>"
    "<script>"
    "function atualizarValores(){fetch('/estado').then(r=>r.json()).then(d=>{document.getElementById('temp_valor').innerText=d.temperatura.toFixed(2);document.getElementById('umidade_valor').innerText=d.umidade.toFixed(2);document.getElementById('pressao_valor').innerText=d.pressao.toFixed(3);document.getElementById('alt_valor').innerText=d.altitude.toFixed(2);}).catch(e=>console.error(e));}"
    "setInterval(atualizarValores,2000);window.onload=atualizarValores;"
    "</script>";

// Contem o formulario da pagina de configuracoes.
const char HTML_CONTENT_CONFIG[] =
    "<main class='container d-flex justify-content-center'>"
        "<div class='card shadow-sm' style='max-width: 800px; flex-grow: 1;'>"
            "<div class='card-body'>"
                "<h2 class='card-title'>Limites e Calibração</h2>"
                "<form id='configForm' class='mt-4'>"
                    "<h4>Temperatura (°C)</h4>"
                    "<div class='row g-3 align-items-center mb-3'>"
                        "<div class='col-md-4 form-grid-item'><label for='temp_min' class='form-label'>Mínimo:</label><input type='number' step='any' id='temp_min' name='temp_min' class='form-control'></div>"
                        "<div class='col-md-4 form-grid-item'><label for='temp_max' class='form-label'>Máximo:</label><input type='number' step='any' id='temp_max' name='temp_max' class='form-control'></div>"
                        "<div class='col-md-4 form-grid-item'><label for='temp_offset' class='form-label'>Offset:</label><input type='number' step='any' id='temp_offset' name='temp_offset' class='form-control'></div>"
                    "</div><hr>"
                    "<h4>Umidade (%)</h4>"
                    "<div class='row g-3 align-items-center mb-3'>"
                        "<div class='col-md-4 form-grid-item'><label for='umid_min' class='form-label'>Mínimo:</label><input type='number' step='any' id='umid_min' name='umid_min' class='form-control'></div>"
                        "<div class='col-md-4 form-grid-item'><label for='umid_max' class='form-label'>Máximo:</label><input type='number' step='any' id='umid_max' name='umid_max' class='form-control'></div>"
                        "<div class='col-md-4 form-grid-item'><label for='umid_offset' class='form-label'>Offset:</label><input type='number' step='any' id='umid_offset' name='umid_offset' class='form-control'></div>"
                    "</div><hr>"
                    "<h4>Pressão (kPa)</h4>"
                    "<div class='row g-3 align-items-center mb-3'>"
                        "<div class='col-md-4 form-grid-item'><label for='press_min' class='form-label'>Mínimo:</label><input type='number' step='any' id='press_min' name='press_min' class='form-control'></div>"
                        "<div class='col-md-4 form-grid-item'><label for='press_max' class='form-label'>Máximo:</label><input type='number' step='any' id='press_max' name='press_max' class='form-control'></div>"
                        "<div class='col-md-4 form-grid-item'><label for='press_offset' class='form-label'>Offset:</label><input type='number' step='any' id='press_offset' name='press_offset' class='form-control'></div>"
                    "</div><hr>"
                    "<h4>Altitude (m)</h4>"
                    "<div class='row g-3 align-items-center mb-3'>"
                        "<div class='col-md-4 form-grid-item'><label for='alt_min' class='form-label'>Mínimo:</label><input type='number' step='any' id='alt_min' name='alt_min' class='form-control'></div>"
                        "<div class='col-md-4 form-grid-item'><label for='alt_max' class='form-label'>Máximo:</label><input type='number' step='any' id='alt_max' name='alt_max' class='form-control'></div>"
                        "<div class='col-md-4 form-grid-item'><label for='alt_offset' class='form-label'>Offset:</label><input type='number' step='any' id='alt_offset' name='alt_offset' class='form-control'></div>"
                    "</div>"
                    "<button type='submit' class='btn btn-primary mt-3'>Salvar Configurações</button>"
                    "<p id='saveStatus' class='mt-2' style='color:green; font-weight:bold;'></p>"
                "</form>"
            "</div></div>"
    "</main>"
    "<script>"
    "window.onload=()=>{fetch('/getconfig').then(r=>r.json()).then(d=>{for(const key in d){let el=document.getElementById(key);if(el)el.value=d[key];}}).catch(e=>console.error('Erro:',e));};"
    "document.getElementById('configForm').addEventListener('submit',e=>{"
        "e.preventDefault();const formData=new FormData(e.target);const status=document.getElementById('saveStatus');"
        "status.textContent='Salvando...';"
        "fetch('/config',{method:'POST',body:new URLSearchParams(formData)})"
        ".then(res=>{if(res.ok)status.textContent='Configurações salvas!';else status.textContent='Falha ao salvar.';setTimeout(()=>status.textContent='',3000);})"
        ".catch(e=>{console.error(e);status.textContent='Erro de comunicação.';});"
    "});"
    "</script>";

// Template generico para as paginas de grafico.
const char HTML_CONTENT_CHART_PAGE[] =
    "<h1 id='page-title'>Gráfico</h1>"
    "<div class='container'><div class='card chart-card'><canvas id='chart'></canvas></div></div>"
    "<script>"
    "const page_configs={"
    "'/temperatura':{key:'temperatura',sufix:'temp',title:'Temperatura',label:'Temperatura (°C)',color:'rgb(255,99,132)',alpha:'rgba(255,99,132,0.2)'},"
    "'/umidade':{key:'umidade',sufix:'umid',title:'Umidade',label:'Umidade (%)',color:'rgb(54,162,235)',alpha:'rgba(54,162,235,0.2)'},"
    "'/pressao':{key:'pressao',sufix:'press',title:'Pressão',label:'Pressão (kPa)',color:'rgb(75,192,192)',alpha:'rgba(75,192,192,0.2)'},"
    "'/altitude':{key:'altitude',sufix:'alt',title:'Altitude',label:'Altitude (m)',color:'rgb(153,102,255)',alpha:'rgba(153,102,255,0.2)'}"
    "};"
    "const config=page_configs[window.location.pathname];"
    "document.getElementById('page-title').textContent='Gráfico de '+config.title;"
    "let chart;"
    "function createChart(limits){"
    "const min_val=limits[config.sufix+'_min'];const max_val=limits[config.sufix+'_max'];"
    "const ctx=document.getElementById('chart').getContext('2d');"
    "chart=new Chart(ctx,{type:'line',data:{labels:[],datasets:[{label:config.label,data:[],borderColor:config.color,backgroundColor:config.alpha,borderWidth:2,fill:true,tension:0.1}]},"
    "options:{plugins:{annotation:{annotations:{"
    "line_min:{type:'line',yMin:min_val,yMax:min_val,borderColor:'red',borderWidth:2,borderDash:[6,6],label:{content:'Mín: '+min_val,enabled:true,position:'start'}},"
    "line_max:{type:'line',yMin:max_val,yMax:max_val,borderColor:'green',borderWidth:2,borderDash:[6,6],label:{content:'Máx: '+max_val,enabled:true,position:'start'}}"
    "}}}}});"
    "}"
    "function addData(d){if(!chart)return;const t=new Date().toLocaleTimeString('pt-BR',{hour:'2-digit',minute:'2-digit',second:'2-digit'});chart.data.labels.push(t);chart.data.datasets[0].data.push(d);if(chart.data.labels.length>%d) {chart.data.labels.shift();chart.data.datasets[0].data.shift();}chart.update('none');}"
    "function atualizarGrafico(){fetch('/estado').then(r=>r.json()).then(d=>addData(d[config.key])).catch(e=>console.error('Erro:',e));}"
    "window.onload=()=>{fetch('/getconfig').then(r=>r.json()).then(limits=>{createChart(limits);atualizarGrafico();setInterval(atualizarGrafico,2000);}).catch(e=>console.error('Erro:',e));};"
    "</script>";

// Tag de fechamento comum a todas as paginas.
const char HTML_FOOTER[] = 
    "<script src='https://cdn.jsdelivr.net/npm/bootstrap@5.3.3/dist/js/bootstrap.bundle.min.js'></script>"
    "</body></html>";

//------------------------------------------------Main------------------------------------------------

/*
 * Inicializa todo os sensores, conecta-se ao Wi-Fi, inicia o servidor web e entra no loop para
 * ler os sensores e manter a conexao.
 */
int main()
{
    stdio_init_all();

    setup();

    // Inicializacao dos sensores I2C.
    i2c_init(I2C_PORT_AHT20, 400 * 1000);
    gpio_set_function(I2C_SDA_AHT, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_AHT, GPIO_FUNC_I2C);
    aht20_init(I2C_PORT_AHT20);

    i2c_init(I2C_PORT_BMP280, 400 * 1000);
    gpio_set_function(I2C_SDA_BMP, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_BMP, GPIO_FUNC_I2C);
    bmp280_init(I2C_PORT_BMP280);
    struct bmp280_calib_param params;
    bmp280_get_calib_params(I2C_PORT_BMP280, &params);

    // Inicializacao e conexao Wi-Fi
    cyw43_arch_init();
    cyw43_arch_enable_sta_mode();
    gpio_put(LED_PIN_RED, 1); // Acende ambos os LEDs (amarelo)
    gpio_put(LED_PIN_GREEN, 1);
    printf("Conectando ao Wi-Fi: %s\n", WIFI_SSID);

    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000))
    {
        printf("Falha ao conectar.\n");
        gpio_put(LED_PIN_GREEN, 0); // Se falhar, acende o LED vermelho e sinaliza com o buzzer.
        tocar_buzzer(300, 1000);
        return 1;
    }

    // Caso consiga se conectar
    printf("Conectado! IP: %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
    gpio_put(LED_PIN_RED, 0); // Acende apenas o LED verde
    tocar_buzzer(1200, 100);
    sleep_ms(50);
    tocar_buzzer(1500, 100);

    // Inicia o servidor web apos a conexao bem-sucedida.
    start_http_server();

    uint32_t last_sensor_read_ms = 0;
    while (true)
    {

        cyw43_arch_poll();
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());

        // Leitura dos sensores, feita a cada 2 segundos.
        if (now_ms - last_sensor_read_ms >= 2000)
        {
            last_sensor_read_ms = now_ms;

            // Leitura dos dados
            int32_t raw_temp_bmp, raw_pressure;
            AHT20_Data data_aht;
            bmp280_read_raw(I2C_PORT_BMP280, &raw_temp_bmp, &raw_pressure);
            float pressure_pa = bmp280_convert_pressure(raw_pressure, raw_temp_bmp, &params);
            aht20_read(I2C_PORT_AHT20, &data_aht);

            // Aplica os offsets de definidos na pagina pelo usuario
            g_temperatura = data_aht.temperature + g_temp_offset;
            g_umidade = data_aht.humidity + g_umid_offset;
            g_pressao = (pressure_pa / 1000.0) + g_press_offset;
            g_altitude = calculate_altitude(pressure_pa) + g_alt_offset;

            // Se os valores atuais passarem dos maximos ou dos minimos, aciona a matriz de LEDs
            bool em_alerta = false;

            if (g_temperatura > g_temp_max || g_temperatura < g_temp_min)
            {
                em_alerta = true;
            }
            if (g_umidade > g_umid_max || g_umidade < g_umid_min)
            {
                em_alerta = true;
            }

            if (em_alerta)
            {
                desenho_pio(alerta1, pio, sm, 1.0, 1.0, 0.0);
                sleep_ms(500);
                desenho_pio(alerta1, pio, sm, 1.0, 1.0, 0.0);
            }
            else
            {
                desenho_pio(matrizVazia, pio, sm, 1.0, 1.0, 0.0);
            }
        }
        sleep_ms(10);
    }
}

//----------------------------------------------Funcoes------------------------------------------------

// Funcao para inicializar os LEDs, buzzer, PIO, botoes e as interrupcoes.
void setup()
{
    gpio_init(LED_PIN_GREEN);
    gpio_set_dir(LED_PIN_GREEN, GPIO_OUT);
    gpio_init(LED_PIN_RED);
    gpio_set_dir(LED_PIN_RED, GPIO_OUT);
    gpio_init(BUZZER_PIN);
    gpio_set_dir(BUZZER_PIN, GPIO_OUT);

    gpio_init(BUTTON_A);
    gpio_set_dir(BUTTON_A, GPIO_IN);
    gpio_pull_up(BUTTON_A);
    gpio_init(BUTTON_B);
    gpio_set_dir(BUTTON_B, GPIO_IN);
    gpio_pull_up(BUTTON_B);

    pio = pio0;
    uint offset = pio_add_program(pio, &blink_program);
    sm = pio_claim_unused_sm(pio, true);
    blink_program_init(pio, sm, offset, WS2812_PIN);

    gpio_set_irq_enabled_with_callback(BUTTON_A, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled_with_callback(BUTTON_B, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
}

// Define as cores da funcao desenho_pio.
uint32_t matrix_rgb(double r, double g, double b)
{
    unsigned char R = r * 255;
    unsigned char G = g * 255;
    unsigned char B = b * 255;
    return ((uint32_t)(G) << 24) | ((uint32_t)(R) << 16) | ((uint32_t)(B) << 8);
}

// Funcao para desenhar na matriz de LEDs.
void desenho_pio(double *desenho, PIO pio, uint sm, float r, float g, float b)
{
    for (int i = 0; i < NUM_PIXELS; i++)
    {
        pio_sm_put_blocking(pio, sm, matrix_rgb(desenho[NUM_PIXELS - 1 - i] * r, desenho[NUM_PIXELS - 1 - i] * g, desenho[NUM_PIXELS - 1 - i] * b));
    }
}

// Toca o buzzer de acordo com a frequencia e duracao definidos.
void tocar_buzzer(uint freq, uint duracao)
{
    if (freq == 0)
        return;
    uint32_t periodo = 1000000 / (freq * 2);
    uint32_t ciclos = (duracao * 1000) / periodo;

    for (uint32_t i = 0; i < ciclos; i++)
    {
        gpio_put(BUZZER_PIN, 1);
        sleep_us(periodo);
        gpio_put(BUZZER_PIN, 0);
        sleep_us(periodo);
    }
}

// Funcao de callback chamada por interrupcao ao pressionar um botao.
void gpio_callback(uint gpio, uint32_t events)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    // Debounce de 500 ms.
    if (now - g_last_press_time < DEBOUNCE_MS)
    {
        return;
    }
    g_last_press_time = now;

    if (gpio == BUTTON_A)
    { // Retorna a pagina
        g_current_page_index--;
        if (g_current_page_index < 0)
        {
            g_current_page_index = G_NUM_PAGES - 1; // Volta ao final da lista.
        }
    }
    else if (gpio == BUTTON_B)
    { // Avanca a pagina
        g_current_page_index++;
        if (g_current_page_index >= G_NUM_PAGES)
        {
            g_current_page_index = 0; // Volta ao inicio da lista.
        }
    }
    // Sinaliza qual eh a proxima pagina.
    g_target_page = G_PAGES[g_current_page_index];
    printf("Botao pressionado, proxima pagina: %s\n", g_target_page);
}

// Calcula a altitude com base na pressao atmosferica.
float calculate_altitude(float pressure_pa)
{
    return 44330.0 * (1.0 - pow(pressure_pa / SEA_LEVEL_PRESSURE, 0.1903));
}

// Envia um pedaco de dados via TCP.
static err_t send_chunk(struct tcp_pcb *tpcb, const char *data)
{
    return tcp_write(tpcb, data, strlen(data), TCP_WRITE_FLAG_COPY);
}

// Monta e envia uma pagina HTML completa.
void send_full_response(struct tcp_pcb *tpcb, const char *content_template)
{
    char final_content[4096];

    if (strstr(content_template, "%d"))
    {
        snprintf(final_content, sizeof(final_content), content_template, MAX_CHART_POINTS);
    }
    else
    {
        strncpy(final_content, content_template, sizeof(final_content));
    }
    final_content[sizeof(final_content) - 1] = '\0';

    char http_header[128];
    int content_len = strlen(HTML_HEADER) + strlen(HTML_NAV) + strlen(final_content) + strlen(HTML_FOOTER);
    snprintf(http_header, sizeof(http_header),
             "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n", content_len);

    send_chunk(tpcb, http_header);
    send_chunk(tpcb, HTML_HEADER);
    send_chunk(tpcb, HTML_NAV);
    send_chunk(tpcb, final_content);
    send_chunk(tpcb, HTML_FOOTER);
    tcp_output(tpcb);
}

// Monta e envia uma resposta JSON.
void send_json_response(struct tcp_pcb *tpcb, const char *payload)
{
    char http_header[128];
    snprintf(http_header, sizeof(http_header),
             "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
             (int)strlen(payload));
    send_chunk(tpcb, http_header);
    send_chunk(tpcb, payload);
    tcp_output(tpcb);
}

// Processa os dados recebidos de um formulario.
void parse_post_data(const char *data)
{
    char buffer[512];
    strncpy(buffer, data, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    char *token = strtok(buffer, "&");
    while (token != NULL)
    {
        char *key = token;
        char *value_str = strchr(token, '=');
        if (value_str)
        {
            *value_str = '\0';
            value_str++;
            float value = atof(value_str);

            if (strcmp(key, "temp_offset") == 0)
                g_temp_offset = value;
            else if (strcmp(key, "temp_min") == 0)
                g_temp_min = value;
            else if (strcmp(key, "temp_max") == 0)
                g_temp_max = value;
            else if (strcmp(key, "umid_offset") == 0)
                g_umid_offset = value;
            else if (strcmp(key, "umid_min") == 0)
                g_umid_min = value;
            else if (strcmp(key, "umid_max") == 0)
                g_umid_max = value;
            else if (strcmp(key, "press_offset") == 0)
                g_press_offset = value;
            else if (strcmp(key, "press_min") == 0)
                g_press_min = value;
            else if (strcmp(key, "press_max") == 0)
                g_press_max = value;
            else if (strcmp(key, "alt_offset") == 0)
                g_alt_offset = value;
            else if (strcmp(key, "alt_min") == 0)
                g_alt_min = value;
            else if (strcmp(key, "alt_max") == 0)
                g_alt_max = value;
        }
        token = strtok(NULL, "&");
    }
}

// Funcao principal de callback para receber dados do servidor TCP.
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        return ERR_OK;
    }

    char request_buffer[1024];
    pbuf_copy_partial(p, request_buffer, sizeof(request_buffer) - 1, 0);
    request_buffer[sizeof(request_buffer) - 1] = '\0';
    tcp_recved(tpcb, p->tot_len);

    if (strstr(request_buffer, "GET /navigate "))
    {
        char json_payload[128];
        if (g_target_page != NULL)
        {
            snprintf(json_payload, sizeof(json_payload), "{\"goto\":\"%s\"}", g_target_page);
            g_target_page = NULL;
        }
        else
        {
            snprintf(json_payload, sizeof(json_payload), "{\"goto\":null}");
        }
        send_json_response(tpcb, json_payload);
    }
    else if (strstr(request_buffer, "POST /config "))
    {
        char *body = strstr(request_buffer, "\r\n\r\n");
        if (body)
        {
            body += 4;
            parse_post_data(body);
        }
        char http_header[] = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
        send_chunk(tpcb, http_header);
        tcp_output(tpcb);
    }
    else if (strstr(request_buffer, "GET /getconfig "))
    {
        char json_payload[512];
        snprintf(json_payload, sizeof(json_payload),
                 "{\"temp_offset\":%.2f,\"temp_min\":%.2f,\"temp_max\":%.2f,"
                 "\"umid_offset\":%.2f,\"umid_min\":%.2f,\"umid_max\":%.2f,"
                 "\"press_offset\":%.2f,\"press_min\":%.2f,\"press_max\":%.2f,"
                 "\"alt_offset\":%.2f,\"alt_min\":%.2f,\"alt_max\":%.2f}",
                 g_temp_offset, g_temp_min, g_temp_max, g_umid_offset, g_umid_min, g_umid_max,
                 g_press_offset, g_press_min, g_press_max, g_alt_offset, g_alt_min, g_alt_max);
        send_json_response(tpcb, json_payload);
    }
    else if (strstr(request_buffer, "GET /estado "))
    {
        char json_payload[128];
        snprintf(json_payload, sizeof(json_payload),
                 "{\"temperatura\":%.2f,\"umidade\":%.2f,\"pressao\":%.3f,\"altitude\":%.2f}",
                 g_temperatura, g_umidade, g_pressao, g_altitude);
        send_json_response(tpcb, json_payload);
    }
    else
    {
        const char *content_to_send = NULL;
        if (strstr(request_buffer, "GET /config "))
        {
            content_to_send = HTML_CONTENT_CONFIG;
        }
        else if (strstr(request_buffer, "GET /temperatura ") || strstr(request_buffer, "GET /umidade ") ||
                 strstr(request_buffer, "GET /pressao ") || strstr(request_buffer, "GET /altitude "))
        {
            content_to_send = HTML_CONTENT_CHART_PAGE;
        }
        else
        {
            content_to_send = HTML_CONTENT_INICIO;
        }
        send_full_response(tpcb, content_to_send);
    }

    pbuf_free(p);
    tcp_close(tpcb);
    return ERR_OK;
}

// Callback chamado quando uma nova conexao TCP eh aceita.
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// Inicializa o servidor HTTP.
static void start_http_server()
{
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);

    if (!pcb)
    {
        printf("Erro ao criar PCB TCP\n");
        return;
    }
    if (tcp_bind(pcb, IP_ANY_TYPE, 80) != ERR_OK)
    {
        printf("Erro ao ligar o servidor na porta 80\n");
        return;
    }

    pcb = tcp_listen(pcb);
    tcp_accept(pcb, tcp_server_accept);
    printf("Servidor HTTP iniciado na porta 80\n");
}