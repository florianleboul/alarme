//bibliothèque série
#include <SoftwareSerial.h>
//bibliothèque LCD
#include <LiquidCrystal.h>
//bibliothèque SPI pour la carte SD
#include <SPI.h>
//bibliothèque SD
#include <SD.h>
//bibliothèque pour le clavier matriciel
#include <Keypad.h>

//instanciation du module GSM sur les pin 2(RX) et 3(TX)
SoftwareSerial gsm(2, 3);
//instanciation du LCD sur les bits de données 8 7 6 5
LiquidCrystal lcd(12, 11, 8, 7, 6, 5);
//instanciation du fichier contenant les informations
File settings;

//CLAVIER MATRICIEL
//4 lignes
const int rows = 4;
//3 colones
const int cols = 3;
//mappage
char keyChar[rows][cols] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'#','0','*'}
};
//PINs connectés aux lignes
byte rowPINs[rows] = {14, 15, 16, 17};
//PINs connectés aux colones
byte colPINs[cols] = {18, 19, 20};
//instanciation du clavier
Keypad matKeypad = Keypad(makeKeymap(keyChar), rowPINs, colPINs, rows, cols);

const int alarm = 21;
const int LEDAlert = 22;
boolean alarmeON = false;
const int putON = 23;

//Déclaration des codes pouvant etre trouvés dans le fichier
// -DIS = distance de détections
// -NUM = listge des numéros de telephone
// -COD = code en codage base 3
// -PIN = code PIN de la carte SIM
String codes[] = {"DIS", "NUM", "COD", "PIN"};
//Distance en centimètres avant déclenchement
int distanceAlarme;
//tableau contenant les numéros à contacter
String numeros[9];
//code de l'alarme
int userCode;
//code PIN
String codePin;
//
int delayBeforeAlert;

int tryNumbs;

//PIN émission ultrasons
const int USSend = 9;
//PIN récéption ultrasons
const int USRec = 10;

//Message venant du GSM
String messageGSM;


void setup() { 
  //initialisation de la communication avec le GSM à 19200bauds/s
  gsm.begin(19200);
  //initialisation du LCD sous forme 16 colones 2 lignes
  lcd.begin(16, 2);
  //initialisation de la communication série à 19200bauds/s
  Serial.begin(19200);
  pinMode(LEDAlert, OUTPUT);
  digitalWrite(LEDAlert, LOW);
  pinMode(alarm, OUTPUT);
  digitalWrite(alarm, LOW);
  pinMode(putON, INPUT);

  //PIN d'émission US en sortie
  pinMode(USSend, OUTPUT);
  //PIN de récéption US en entrée
  pinMode(USRec, INPUT);
  //PIN d'émission US à l'état bas
  digitalWrite(USSend, LOW);

  matKeypad.setDebounceTime(100);
}

void loop() {
  getFromGSM();
  if(!digitalRead(putON)){
    alarmeON = true;
  }
  if(alarmeON){
    while(verification());
    if(alarmeON){
      alert();  
    } 
  }
}


/**
   détéction calcule la distance de l'alarme à la cible, et renvoie un booleen correspondant à la différence entre distance mesurée et distance d'intrusion
   calcul :
    -durée totale = 1 aller-retour -> dt
    -durée totale/2 = 1 aller
    -µs/1000000 = s
    -c = 340m/s
    -((dt/2)/1000000)*340 = distance
   @return booleen indiquand si la distance d'intrusion est franchie
*/
boolean detection() {
  delay(100);
  //PIN d'émission à l'état haut
  digitalWrite(USSend, HIGH);
  //pendant 10µs
  delayMicroseconds(10);
  //PIN d'émission à l'état bas
  digitalWrite(USSend, LOW);
  //création et initialisation de la variable avec la durée entre l'émission et la récéption
  unsigned long duree = pulseIn(USRec, HIGH);
  return (duree > 30000 && (((duree / 2) / 1000000.0) * 340) <= distanceAlarme);
}

void alert(){
  digitalWrite(LEDAlert, HIGH);
  digitalWrite(alarm, HIGH);
  sendSMS("ALERTE INTRUSTION", numeros);
  while(!verificationAfterAlert);
  digitalWrite(LEDAlert, LOW);
  digitalWrite(alarm, LOW);  
  alarmeON = false;
}

boolean getFromGSM(){
  messageGSM = "";
  if(gsm.available()){
    messageGSM+=gsm.readString();
  }
  if (getPartOfString(messageGSM, '+', 1, 4).equals("CMTI")){
    gsm.print("AT+CMGR=");
    gsm.println(getPartOfString(messageGSM, ',', 1, 2));

  }
  if ((getPartOfString(messageGSM, '+', 1, 4).equals("CMGR")) && isNumberPresent(getPartOfString(messageGSM, '+', 3, 11))){
    if (getPartOfString(messageGSM, '+', 5, 2).equals("ON")){
      alarmeON = true;
    }else if (getPartOfString(messageGSM, '+', 5, 3).equals("OFF")){
      alarmeON = false;
    }
  }
  return alarmeON;
}
/**
 * verificationAfterAlert boucle tant que le bon mot de passe n'a pas été rentré
 * @return boolean  égalité des mots de passe
 */
boolean verificationAfterAlert(){
  int guestCode = 0000;
  int rang = 3;
  while(guestCode != userCode){
    if(!getFromGSM()){
      return true;
    }
    char keyPressed = matKeypad.getKey();
    if(ascii2int(keyPressed) >= 0 && ascii2int(keyPressed) <= 9){
      guestCode+=keyPressed*puissance(10,rang);
      rang--;
      if(rang = 0)rang = 3;
    }
  }
  return (guestCode == userCode);
}

/**
 * verification vérifie l'état des capteurs, si l'un est déclenché, 
 * on dispose alors du temps défini et du nombre d'essais définis pour désactiver l'alarme
 * @return boolean  état de la vérification
 */
boolean verification(){
  boolean ret = true;
  if(detection()){
    int guestCode = 0000;
    unsigned int time = millis();
    int rang = 3;
    int trys = tryNumbs;
    while(guestCode != userCode && millis() - time < delayBeforeAlert && trys > 0){
      char keyPressed = matKeypad.getKey();
      if(ascii2int(keyPressed) >= 0 && ascii2int(keyPressed) <= 9){
        guestCode+=keyPressed*puissance(10,rang);
        rang--;
        if(rang = 0){
          rang = 3;
          trys--;
        }
      }
    }
    if(guestCode == userCode){
      alarmeON = false;
      ret = true;
    }
    else ret = false;
  }
  return ret;
}

boolean isNumberPresent(String number){
  boolean ret = false;
  for(int i = 0; i < sizeof(numeros); i++){
    if(numeros[i].equals(number)){
        ret = true;
        break;
    }
  }
  return ret;
}
/**
   sendSMS envoie par SMS le message message à tous les numéros présents dans le tableau numeros
   @param message message à envoyer
   @param numeros numeros auxquels envoyer le message
*/
void sendSMS(String message, String numeros[]) {
  //si le message est différent de nul, différent d'une chaine vide et qu'il y a des numeros de telephone
  if (message != NULL && !message.equals("") && numeros != NULL) {
    //pour chaque numéro de téléphone
    for (int i = 0; i < sizeof(numeros); ++i) {
      //on indique qu'on va envoyer un message
      gsm.print("AT+CMGS=+");
      //au numéro de telephone correspondant au tour de boucle
      gsm.println(numeros[i]);
      //on attend 500ms
      delay(500);
      //on envoie le message au GSM
      gsm.println(message);
      //on clôture le message et on l'envoie
      gsm.write(0x1A);
    }
  }
}

/**
   getPartOfString récupère un message, et renvoie les nbChar caractères après occurences occurences du caractère delimiter, avec ou sans codes d'erreur
   @param  String   message    message de base
   @param  char     delimiter  caractère délimiteur
   @param  int      occurences nombre d'occurences
   @param  int      nbChar     nombre de caractères à récupérer
   @return String   ret        partie du message renvoyée
*/
String getPartOfString(String message, char delimiter, int occurences, int nbChar) {
  //on déclare le retour à une chaine vide
  String ret = "";
  //on copie le nombre d'occurences dans une variable locale
  int delimiterOccurences = 0;
  //on initialise l'indice de début de données à -1
  int startIndice = -1;
  if (message != NULL && !message.equals("")) {
    //pour chaque caractère du message
    for (int i = 0; i < message.length(); i++) {
      //on vérifie l'équivalence avec le caractère delimiter
      if (message.charAt(i) == delimiter) {
        //cas favorable : on incrémente le nombre d'occurences trouvées
        delimiterOccurences++;
      }
      //si on a trouvé le bon nombre d'occurences
      if (delimiterOccurences == occurences) {
        //on démarre la lecture à l'indice courant
        startIndice = i + 1;
        break;
      }
    }
    //si on a trouvé un indice de début
    if (startIndice != -1) {
      //si on ne risque pas de sortir de la chaîne
      if ((startIndice + nbChar) < message.length()) {
        //pour chaque caractère de startIndice à startIndice+nbChar
        for (int i = startIndice; i < startIndice + nbChar; ++i) {
          //on concatène le caractère au retour
          ret += message.charAt(i);
        }
      }
    }

  }
  return ret;
}

/**
   viderLCD permert d'effacer les lignes de l'écran :
    -1 efface la première ligne
    -2 efface la seconde ligne
    -3 efface les deux lignes
   @param int   i lignes à effacer
*/
void viderLCD(int i) {
  //on switch la valeur de 1
  switch (i) {
    //pour i = 1
    case 1:
      //on se place première case première ligne
      lcd.setCursor(0, 0);
      //on affiche des espaces jusqu'à la fin de la ligne
      for (int i = 0; i < 16; ++i) {
        lcd.print("");
      }
      break;
    //pour i = 2
    case 2:
      //on se place première case seconde ligne
      lcd.setCursor(0, 1);
      //on affiche des espaces jusqu'à la fin de la ligne
      for (int i = 0; i < 16; ++i) {
        lcd.print("");
      }
      break;
    case 3:
      //on se place première case première ligne
      lcd.setCursor(0, 0);
      //on change de ligne
      for (int i = 0; i < 2; ++i) {
        //on affiche des espaces jusqu'à la fin de la ligne
        for (int i = 0; i < 16; ++i) {
          lcd.print("");
        }
      }
      break;
  }
}


void decode() {
  //on crée et initialise le contenu du ficher a chaine vide
  String messageSD = "";
  //si la carte ne répond pas on quitte
  if (!SD.begin(4)) {
    return;
  }
  //on ouvre le fichier settings.txt dans la variable settings
  settings = SD.open("settings.txt");
  if (settings) {
    //tant qu'on a une activité sur le fichier
    while (settings.available()) {
      //on stock le contenu du fichier dans le variable
      messageSD += settings.read();
    }
    //on ferme le fichier
    settings.close();
  }
  //si le message n'est pas celui d'origine
  if (!messageSD.equals("")) {
    //on affecte les valeurs
    affecterValeurs(messageSD);
  }
}

/**
 * affecterValeurs affecte les valeurs des variables au code a partir d'une String
 * @param  String message       chaine de caractère contenant les inormations
 */
void affecterValeurs(String message) {
  char nbInfos[3];
  //on récupère le nombre d'infos du message
  string2charArray(getPartOfString(message, '+', 1, 3), nbInfos);
  //on crée et initialise le compteur de paramètres
  int nbSettings = charTab2int(nbInfos, sizeof(nbInfos));
  //pour chaque ligne de la seconde jusqu'a 2+nbSettings
  for (int i = 1; i <= nbSettings; ++i) {
    //on récupère le code contenu sur la ligne
    String codeSetting = getPartOfString(message, '+', i + 1, 3);
    
    //si le code est : DIS
    if (codeSetting.equals(codes[0])) {
      char tabDIS[3];
      //on récupère 3 caractères pour la distance
      string2charArray(getPartOfString(message, ':', i, 3), tabDIS);
      //on les transforme en un entier
      distanceAlarme = charTab2int(tabDIS, sizeof(tabDIS));
    }
    //si le code est NUM
    else if (codeSetting.equals(codes[1])) {
      //on récupère le nombre de numéros inscrits
      int nbNumbs = ascii2int(getPartOfString(message, ':', i, 1).charAt(0));
      for (int i = 1; i <= nbNumbs; i++) {
        //on récupère les numéros pour les stocker dans le tableau numeros
        numeros[i - 1] = getPartOfString(message, '!', i, 11);
      }
    }
    else if (codeSetting.equals(codes[2])) {
      //on crée un tableau pour stocker le code en base 3
      char tabCOD[9];
      //on récupère les numéros de telephone
      string2charArray(getPartOfString(message, ':', i, 9), tabCOD);
      //on stock le décodage dans la variable cod
      userCode = base3Decode(tabCOD, sizeof(tabCOD));
    } else if (codeSetting.equals(codes[3])) {
      codePin = getPartOfString(message, ':', i, 4);
    }

  }
}

/**
   ascii2int renvoie l'entier associé à un code ascii, s'il existe
   @param  char   ascii caractère à vérifier
   @return       entier correspondant, ou 0
*/
int ascii2int(char ascii) {
  //si le caractère est un chiffre, en renvoit sa valeur en entier (caractère - 48 = entier)
  return (ascii - 48 >= 0 && ascii - 48 <= 9) ? ascii - 48 : -1;
}

/**
 * charTab2int utilise la fonction ascii2int sur un tableau de caractères, pour le transformer en un entier
 * @param   char* charTab       tableau à convertire
 * @param   int   tabSize       taille du tableau à convertir
 * @return  int   ret           résultat de la conversion sous forme d'entier
 */
int charTab2int(char* charTab, int tabSize) {
  //retour initialisé à 0
  int ret = 0;
  //pour chaque caractère
  for (int i = 0; i < tabSize; i++)
    //on multiplie l'équivalent numérique du caractère par une puissance de 10 décroissante
    ret += ascii2int(charTab[i]) * puissance(10, tabSize - i - 1);
  return ret;
}

/**
 * base3Decode décode les entiers en base 3 contenus dans un tableau de caractères pour retourner en entier en base 10
 * @param base3  tableau de caractères en base 3
 * @return ret   entier en base 10
 */
int base3Decode(char base3[], int tabSize) {
  boolean verif = true;
  int ret = 0;
  //pour chaque case du tableau de caractères
  for (int i = 0; i < tabSize; i++) {
    //si ce n'est pas un chiffre la vérification est fausse
    if (ascii2int(base3[i]) > 9 || ascii2int(base3[i]) < 0) {
      verif = false;
      break;
    }
  }
  //si on a passé la vérification
  if (verif) {
    //pour chaque case du tableau
    for (int i = 0; i < tabSize; i++) {
      //on ajoute la valeur contenue multipliée par une puissance de 3 décroissante
      ret += (ascii2int(base3[i])) * puissance(3, tabSize - i - 1);
    }
  }
  return ret;
}

/**
 * puissance calcule la puissance d'une base par un exposant
 * @param   int base          base de calcul
 * @param   int puissance     puissance de la base
 * @return  int ret           résultat
 */
int puissance(int base, int exposant) {
  int ret = 1;
  if (exposant > 0) {
    for (int i = 0; i < exposant; i++) {
      //on multiplie par la base autant de fois que l'exposant
      ret *= base;
    }
  }
  return ret;
}

/**
 * string2charArray remplit un tableau donné avec les caractères d'une chaine
 * @param  String message       chaine à insérer
 * @param  char*  charTab       tableau où insérer
 */
void string2charArray(String message, char* charTab) {
  //pour chaque caractère du message, on le stock dans le tableau
  for (int i = 0; i < message.length(); i++) charTab[i] = message.charAt(i);
}