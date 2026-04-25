**Organização CAN**



**can\_frame**



É o formato comum de frame usado internamente.

Representa um frame CAN bruto (ID, DLC, dados).

É a estrutura básica usada por todos os outros módulos.



**can\_ids**



Diz “que mensagem é essa” e quais regras ela deve seguir.

Define os IDs das mensagens e seus metadados (DLC, nó emissor, período, timeout).

É o vínculo entre o código e o DBC.



**can\_codec**



Responsável por converter sinais <-> payload CAN.

Empacota e desempacota bits/bytes conforme o DBC.



**can\_rx / can\_tx**



Camada de interface do REB com o barramento:



can\_rx → recebe frames e encaminha para processamento

can\_tx → monta e envia frames a partir da lógica do REB



**can\_monitor**



Acompanha validade temporal e presença das mensagens.

Gerencia o estado das mensagens recebidas:



timeout

atualização (freshness)

validação (ex: mensagem obrigatória ausente)



**can\_socket\_transport**



Faz a ponte entre CAN e rede:



serializa can\_frame\_t → bytes

desserializa bytes → can\_frame\_t

envia/recebe via socket


-------------------
**Fluxo de transmissão

\*** a lógica do REB decide enviar uma mensagem

* **can\_tx** seleciona qual mensagem será transmitida
* **can\_ids** fornece ID, DLC e metadados
* **can\_codec** empacota os sinais no payload
* isso gera um **can\_frame\_t**
* **can\_socket\_transport** serializa e envia pela rede



\------------------

**Fluxo de recepção**



* **can\_socket\_transport** recebe bytes da GUI e reconstrói um **can\_frame\_t**
* **can\_rx** recebe esse frame
* **can\_ids** identifica que mensagem é aquela e quais regras ela tem
* **can\_monitor** registra se a mensagem chegou no tempo certo e se continua válida
* **can\_codec** extrai os sinais do payload
* a lógica do REB usa esses sinais já interpretados
