/* 

|--------------------------------------------------------------|
|          Documentation du protocole utilisé entre            |
|     la centrale domotique et le module radio (Arduino)       |
|--------------------------------------------------------------| 

>> Ordinateur --> Arduino

  > Syntaxe du message
  
  [commande][separator][arguments1-1]...[argument1-n][separator][argument2][end]
  
  > Syntaxe des bytes
  
  - 1******* : [byte] de contrôle
    - arguments_number : le nombre d'arguments à attendre
    - 10000001 : [set]       / commande de configuration
                               - argument 1 : durée minimum à considérer comme valable
                               - argument 2 : durée maximale à considérer comme valable
                                              (pas de maximum si elle est nulle)
                               - argument 3 : taille minimum pour considérer qu'une 
                                              suite est valable et peut être envoyée 
                                              à l'ordinateur
    - 10000010 : [send]      / commande de transmission radio
                               - argument 1 : nombre de répétitions du message radio
                               - argument 2 : durée d'un "bit radio"
                               - argument 3..(n+2) : symboles (1er byte longueur du symbole en bits)
                               - argument (n+3) : message codé en symboles  
    - 11111110 : [separator] / séparateur d'arguments
    - 11111111 : [end]       / fin d'un appel
  - 0******* : [byte] de données (réduit à 7 bits de données utiles)

>> Sens Arduino --> ordinateur

*/

// taille maximale du tableau stockant une requête, en bytes
#define REQUEST_MAX_SIZE 200
// nombre maximal d'argumants par fonction, chacun prenant un byte 
#define ARGUMENTS_MAX_NUMBER 100
// taille maximale de la séquence envoyée à l'ordinateur, en bytes
#define SEQUENCE_MAX_SIZE 200 

// Hardware parameters
int tx = 4;
int rxInterrupt = 0;
int led = 13;

// Tx variables
byte request [REQUEST_MAX_SIZE];
byte argumentsIndex [ARGUMENTS_MAX_NUMBER];
unsigned int argumentsNumber;
unsigned int repetitionsNumber;
unsigned int baseDuration;
unsigned int logSymbolsNumber;
unsigned int symbolsNumber;
unsigned int symbolIndex;
unsigned int symbolIndexLength;
int i,j,k,m;

// Rx settings
unsigned int minDuration = 100;
unsigned int maxDuration = 5000;
unsigned int minSequenceLength = 100;

// Rx variables
volatile unsigned long duration = 0;
volatile unsigned long lastTime = 0;
volatile unsigned int bufferIndex = 0;
volatile byte buffer [SEQUENCE_MAX_SIZE];
int v;
volatile boolean bufferFlag = false;

void clearVariables() {
  for(i = 0 ; i < REQUEST_MAX_SIZE ; i++) {
    request[i] = 0;
  }
  for(i = 0 ; i < ARGUMENTS_MAX_NUMBER ; i++) {
    argumentsIndex[i] = 0;
  }
  argumentsNumber = 0;
  repetitionsNumber = 0;
  baseDuration = 0;
  logSymbolsNumber = 0;
  symbolsNumber = 0;
  symbolIndex = 0;
  symbolIndexLength = 0;
}

void processSerialRequest() {
  digitalWrite(led, HIGH);
  // 1 - on récupère la requête dans son intégralité,
  // en indexant les arguments qu'elle contient
  argumentsNumber = 0;
  int i = 1;
  while(request[i-1] != B11111111) {
    //Serial.readBytesUntil(B11111111, (char*)request, 200);
    if(Serial.available()) {
      request[i] = Serial.read();
      
      //Serial.println(request[i], BIN);
      
      if(((request[i] & B11111000) >> 3) == B00011111 && request[i] != B11111111) {
        argumentsIndex[argumentsNumber] = i + 1;
        argumentsNumber++;
        
        //Serial.println("inc argNumber");
      
      }
      if(request[i] == B11111111) {
        argumentsIndex[argumentsNumber] = i;
      }
      i++;
    }
  }
  // 2 - on traite la requête selon son type
  switch(request[0]) {
    case B10000001:
      executeSetRequest();
      break;
    case B10000010:
      executeSendRequest();
      break;
  }
  digitalWrite(led, LOW);
  clearVariables();
}

/* Appelée lorsqu'une commande de type set (10000001) est reçue.
Elle permet de configurer les paramètres de réception de ce module
radio avec les paramètres fournis par l'utilsateur. */
void executeSetRequest() {
  minDuration = (int)(request[argumentsIndex[0]]&B01111111);
  if(argumentsIndex[1] > argumentsIndex[0] + 2) {
    minDuration = (minDuration << 7) + (int)(request[argumentsIndex[0]+1]&B01111111);
  }
  
  maxDuration = (int)(request[argumentsIndex[1]]&B01111111);
  if(argumentsIndex[2] > argumentsIndex[1] + 2) {
    maxDuration = (maxDuration << 7) + (int)(request[argumentsIndex[1]+1]&B01111111);
  } 
  
  minSequenceLength = (int)(request[argumentsIndex[2]]&B01111111);
  if(argumentsIndex[3] > argumentsIndex[2] + 2) {
    minSequenceLength = (minSequenceLength << 7) + (int)(request[argumentsIndex[2]+1]&B01111111);
  }
}

/* Appelée lorsqu'une commande de type send (10000010) est reçue.
Elle permet de transmettre tout ce que l'utilisateur envoie jusqu'à 
la réception du bit de fin (11111111)
*/
void executeSendRequest() {

  repetitionsNumber = (int)(request[argumentsIndex[0]]&B01111111);
  if(argumentsIndex[1] > argumentsIndex[0] + 2) {
    repetitionsNumber = (repetitionsNumber << 7) + (int)(request[argumentsIndex[0]+1]&B01111111);
  }
  
  baseDuration = (int)(request[argumentsIndex[1]]&B01111111);
  if(argumentsIndex[2] > argumentsIndex[1] + 2) {
    baseDuration = (baseDuration << 7) + (int)(request[argumentsIndex[1]+1]&B01111111);
  }
   
  logSymbolsNumber = 16;
  symbolsNumber = argumentsNumber - 3;
  //Serial.println(symbolsNumber);
  while((symbolsNumber>>15) != 1) {
    symbolsNumber <<= 1;
    logSymbolsNumber--;
  }
  logSymbolsNumber--;
  //Serial.println(logSymbolsNumber);
  
  // On parcourt l'ensemble du dernier argument (le message codé en symboles) pour l'envoyer 
  for(i = argumentsIndex[argumentsNumber-1] ; i < argumentsIndex[argumentsNumber]; i++) {
    for(j = 6 ; j >= 0 ; j--) {
      if(i > argumentsIndex[argumentsNumber-1] || ((i == argumentsIndex[argumentsNumber-1]) && ((6-j) >= (request[argumentsIndex[argumentsNumber-1]-1] & B00000111)))) {        
        symbolIndex = (symbolIndex<<1) + ((request[i]>>j)&1);
        symbolIndexLength++;
        if(symbolIndexLength == logSymbolsNumber) {
          // envoi du symbole
          for(k = argumentsIndex[symbolIndex+2] ; k < argumentsIndex[symbolIndex+3]-1 ; k++) {
            for(m = 6 ; m >= 0 ; m--) {
              if(k > argumentsIndex[symbolIndex+2] || (k == argumentsIndex[symbolIndex+2] && ((6-m) >= (request[argumentsIndex[symbolIndex+2]-1] & B00000111)))) {
                ((request[k] >> m) & 1) == 1 ? digitalWrite(tx, HIGH) : digitalWrite(tx, LOW) ;
                //Serial.print(1)
                //Serial.print(0)
                delayMicroseconds(baseDuration);
              }
            }
          }
          symbolIndex = 0;
          symbolIndexLength = 0;
        }
      }
    }
  }
}

/* Stocke la durée depuis la dernière interruption dans le tableau 
de réception si cette durée est comprise dans les bornes données. 
Sinon, elle remet à zéro l'indice d'écriture dans le tableau de 
réception. */
void processInterrupt() {
  if(!bufferFlag) {
    duration = micros() - lastTime;
    lastTime = micros();
  
    if(bufferIndex < (SEQUENCE_MAX_SIZE-1) && (duration > minDuration && duration < maxDuration)) {
      // on travaille sur des multiples de 32 pour les durées reçues
      // de plus, on ne travaille que sur 7 bits pour les données
      buffer[bufferIndex] = (byte)((duration>>5)&B01111111);
      bufferIndex++;
    }
    else {
      if(bufferIndex > minSequenceLength) {
        bufferFlag = true;
      }
      else
      {
        bufferIndex = 0; 
      }
    }
  }
}

void setup() { 
  Serial.begin(9600);
  pinMode(tx, OUTPUT);
  attachInterrupt(rxInterrupt, processInterrupt, CHANGE);
}

void loop() {
  if(Serial.available()) {
    request[0] = Serial.read(); 
    // on ne traite la requête que si elle signifie quelque chose
    if(request[0] == B10000001 || request[0] == B10000010) {
      processSerialRequest();
    }
  }
  if(bufferFlag) {
    Serial.write(B10000011);
    //Serial.print('\n');
    for(v = 0 ; v < bufferIndex ; v++) {
      Serial.write(buffer[v]);
      //Serial.print('\n');
    }
    Serial.write(B11111111);
    //Serial.print('\n\n\n');
    bufferIndex = 0;  
    bufferFlag = false;
  }
}
