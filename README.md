# Sistema de monitoramento de incêndios remoto
Com o crescimento da indústria de IoT, novas possibilidades têm surgido para as demais áreas da ciência humana, não seria diferente para a saúde. Este trabalho tem por finalidade a demonstração e simulação de um sistema de monitoramento de pacientes remotamente. A ideia básica é a de um sistema composto um dispositivo capaz de monitorar sinais específicos de um paciente através de sensores e de enviar tais dados em broadcast, de forma que algum outro dispositivo, capaz de entender o protocolo de comunicação CoAP, possa obter os valores de seus sensores. 

Todo projeto que pode ser simulado no Cooja precisa de um arquivo Make, que não é diferente do [CMakefiles](https://pt.wikibooks.org/wiki/Programar_em_C/Makefiles) comum, com a exceção da necessidade da inclusão do “Makefile.include” que contém definições dos arquivos C do núcleo do sistema Contiki. Este arquivo sempre reside na raiz da árvore de fontes Contiki.

Dependências:
- Instant Contiki OS v2.7
- VMWare Player 15
- Windows 10 64bit Host
- Firefox Browser

Inicialmente é preciso realizar uma configuração no firefox, pois as versões mais novas do browser não permitem a  WebExtensions API e precisaremos dela para a instalação do Copper (Cu), que é uma extensão que entende o protocolo CoAP, para teste do servidor RESTFul. A configuração do firefox pode ser feita seguindo o tutorial do repositório do próprio autor da extensão, na sessão “How to integrate the Copper sources into Firefox:”, través deste link: <https://github.com/mkovatsc/Copper>.

Mais informações no [relatório](https://docs.google.com/document/d/1uCny5UV4w0WcF7V43q6eWMrX8I_Bhwa5XFeSUmKBZEo/edit?usp=sharing).
