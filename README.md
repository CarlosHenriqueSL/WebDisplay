# Leitura de sensores com a Raspberry Pi Pico W
**EMBARCATECH - Fase 2**

**Interface Web com exibição de dados**

## Desenvolvedor
- **Carlos Henrique Silva Lopes**

---

### 📄 Descrição

Este projeto utiliza dois sensores para captura de dados do ambiente: BMP280 para a medição da pressão atmosférica e o AHT20 para a medição da umidade e temperatura. Com base na medição da pressão, é possível também determinar a altura com relação ao nível do mar.
Uma vez feitas, essas leituras são exibidas em uma interface web, acessível pela conexão Wi-Fi da Raspberry Pi Pico W, que torna a página HTML do sistema acessível pelo seu endereço IP.
Os dados são organizados em uma página principal, mas também podem ser vistos em gráficos.

---

### 🎯 Objetivo Geral

Capturar dados do ambiente e exibi-los em uma interface web, utilizando conexão Wi-Fi.

---

### ⚙️ Funcionalidades

* **Leitura de sensores:** O sistema usa comunicação I2C para fazer a leitura de dois sensores: BMP280 (conectado ao I2C1) e AHT20 (conctado ao I2C0).
* **Interface Web:** Utilizando o IP da Raspberry Pi Pico W, é possível estabelecer conexão com o servidor web do sistema. Ele mostra e atualiza os dados lidos, utilizando valores brutos e gráficos de linhas. A interface também permite ajustes de valores máximos/mínimos e offsets.
* **Botões:** Os botões A e B da placa BitDogLab foram usados para navegação da interface web. O botão B avança uma página, enquanto o botão A retorna uma página.
* **Buzzer e LEDs RGB:** O buzzer e os LEDs vermelho e verde fazem a sinalização de quando a conexão da placa com a rede Wi-Fi for bem sucedida ou não.
* **Matriz de LEDs:** Caso algum dos dados de temperatura ou umidade estiver acima do seu máximo ou abaixo de seu mínimo, a matriz de LEDs acende, mostrando um alerta (!) em amarelo.

---

### 📌 Mapeamento de Pinos

| Função             | GPIO |
| ------------------ | ---- |
| WS2812 (Matriz)    | 7    |
| Botão A            | 5    |
| Botão B            | 6    |
| LED Verde          | 11   |
| LED Vermelho       | 13   |
| Buzzer             | 21   |

---


### Principais Arquivos
- **`EstacaoMeteorologica.c`**: Contém a lógica principal do programa. Nele estão os arquivos HTML, a conexão com o Wi-Fi, criação da interface web e leitura dos sensores.
- **`lib/`**: Contém os arquivos necessários para utilização dos sensores, desenho na matriz de LEDs e conexão com Wi-Fi.
- **`blink.pio`**: Contém a configuração em Assembly para funcionamento do pio.
- **`README.md`**: Documentação detalhada do projeto.
