/* Minitel Revival, Grehack 2015.
 *
 * Ajuster le nombre de Minitels avec la constante NODE
 *
 * Changer les ports série sur lesquels les Minitel avec les constantes port1 port2 etc.
 *
 * Compilation : gcc -Wall -lpthread Serveur9.c -o Serveur9
 *
 * Remerciements : Frédéric BISSON (https://github.com/Zigazou/PyMinitel) 
 *                 pour sa lib Python dont je me suis inspiré pour le champ de saisie
 *
 *
 *     Copyright (C) 2015 Paget Philippe
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License along
 *     with this program; if not, write to the Free Software Foundation, Inc.,
 *     1 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * 
*/



#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>


// nbr de nodes
#define NODENBR 8
#define NODE 5


// var pour les nodes
#define BUFFSIZE 100*1024
typedef struct 
{
	int num;
	pthread_t thread;
	int killMe;

	int fdSP;
	struct termios termio;

	char buffer1[BUFFSIZE];
	char buffer2[BUFFSIZE];
	char buffer3[BUFFSIZE];

	// champ text :
	int posx, posy, largeur, hauteur, longueur_visible, longueur_totale, curseur_x, decalage, activable, accent, couleur, champ_cache;
	char valeur[1024];

	int posAD;

} dataNode;
dataNode dnn[NODENBR];


// AD
#define fichierAnnonces    "ADs/ads.txt"
#define fichierAnnoncesTmp "ADs/ads.txt.tmp"
#define fichierAnnoncesOld "ADs/ads.txt.old"
// -> len reel -1
#define pseudoT 11
#define titreT  26
#define corpsT  81

typedef struct
{
        char pseudo[pseudoT];
        char titre[titreT];
        char corps[corpsT];
} dataAD;

dataAD *ptrAD,*ptr2AD;
FILE * fdAD;
int lenFichAD, nbrAD;

pthread_mutex_t mutexAD = PTHREAD_MUTEX_INITIALIZER;



int lenFile(char * nom)
{
        FILE *fp;
        int file_size=0;
        if ((fp = fopen(nom, "rb" )) == NULL)
        {
                printf("Thread-init: Fichier AD %s introuvable, taille à 0.\n", nom);
                return(file_size);
        }
        if (fseek(fp, (long)(0), SEEK_END) != 0)
        {
                fclose(fp);
                return(file_size);
        }
        file_size = (ftell(fp));
        fclose(fp);
        return(file_size);
}


void zeroDN(dataNode * dn)
{
	dn->num=dn->killMe=dn->posx=dn->posy=dn->largeur=dn->longueur_visible=dn->longueur_totale=dn->curseur_x=dn->decalage=dn->activable=dn->accent=dn->couleur=dn->champ_cache=dn->posAD = 0;
	memset(dn->buffer1,0,BUFFSIZE);
	memset(dn->buffer2,0,BUFFSIZE);
	memset(dn->valeur,0,1024);
}

char tolow(char car)
{
	if ((car <= 'Z') && (car >= 'A'))
		return (car + 0x20);
	else
		return (car);
}

void strlwr(char * buffer)
{
	int i;
	for(i = 0; buffer[i]; i++)
	{
		buffer[i] = tolow(buffer[i]);
	}
}

// serial port
#define BAUDRATE B1200
#define port1 "/dev/ttyUSB0"
#define port2 "/dev/ttyUSB1"
#define port3 "/dev/ttyUSB2"
#define port4 "/dev/ttyUSB3"
#define port5 "/dev/ttyUSB4"
#define port6 "/dev/ttyUSB5"
#define port7 "/dev/ttyUSB6"
#define port8 "/dev/ttyUSB7"
char * serialPorts[NODENBR] = { port1, port2, port3, port4, port5, port6, port7, port8 };

int min(int a, int b)
{
	if ( a > b )
		return(b);
	return(a);
}

int max(int a, int b)
{
	if ( a > b )
		return(a);
	return(b);
}

void displayHex(char * buffer, int len)
{
	int i;

	for ( i=0 ; i<len ; i++)
		printf("%02x ",buffer[i]);
	printf("\n");

}

void initSerialPorts(int nbr)
{
	int i=0;
	printf("Init serial ports.\n");
	for( ; i<nbr ; i++)
	{
		dnn[i].fdSP = open(serialPorts[i], O_RDWR | O_NOCTTY );
		if (dnn[i].fdSP <0)
		{
			perror(serialPorts[i]);
			exit(1);
		}
		dnn[i].termio.c_cflag = BAUDRATE | CS7 | CREAD | PARENB ;

		//dnn[i].termio.c_lflag = ICANON;
		dnn[i].termio.c_cc[VMIN] = 1;
		dnn[i].termio.c_cc[VTIME] = 0;
		//dnn[i].termio.c_cflag &= ~CRTSCTS;;

		tcflush(dnn[i].fdSP, TCIOFLUSH);
		tcsetattr(dnn[i].fdSP,TCSANOW,&dnn[i].termio);
	}

}

void flushInput(int fd)
{
	tcflush(fd,TCIFLUSH);
}

void sendByte(int fd, char byte)
{
	if (write(fd,&byte,1) != 1 )
		printf("Erreur envoie octet sur le port serie %d\n",fd);
}

int sendBuf(int fd, char* buffer, int len)
{
        int i=0;
        while(i<len)
        {
                //if (!buffer[i])
                //        break;
                if (write(fd,&buffer[i++],1) != 1 )
			printf("Erreur envoie octet sur le port serie %d\n",fd);
		//usleep(5000); 
	}
	return (i);
}

int sendStr(int fd, char* buffer)
{
        int len,i=0;
	len=strlen(buffer);
        while(i<len)
        {
                if (!buffer[i])
                        break;
                if (write(fd,&buffer[i++],1) != 1 )
			printf("Erreur envoie octet sur le port serie %d\n",fd);
		//usleep(5000); 
	}
	return (i);
}

char readByte(int fd)
{
	char res;
	read(fd,&res,1);
	return(res);
}

void readBuf(int fd, char * buffer, int  len)
{
        int i=0;
        while(i<len)
        {
		if (read(fd,&buffer[i++],1) != 1)
			printf("Erreur lecture octet sur le port serie %d\n",fd);
	}
}

void discardByte(int fd, int len)
{
	int i;
	printf("Discard : ");
	for ( i=0 ; i<len ; i++)
		printf("%02x ", readByte(fd));
	printf("\n");
}

unsigned short getKey(dataNode * dn)
{
	//unsigned short res;
	int res,tmp;
	int fd = dn->fdSP;

	res = readByte(fd);

	if (res == 0x1B)
	{ // touce bizarres fleche etc.
		//printf("getKey 1B\n");
		res = readByte(fd);
		switch (res)
		{
			case 0x5B :
				//printf("getKey 5B\n");
				res*=256;
				tmp=readByte(fd);
				//printf("getKey %02x\n",tmp);
				res+=tmp;
				break;
			default : // 1B ?? ?? = '*'
				readByte(fd);
				res='*';
				break;
		}
	} 
	else if (res == 0x13 )
	{ // touche navigation
		res*=256;
		tmp=readByte(fd);
		//printf("getKey 0x13 %02x\n",tmp);
		if (tmp == 0x59) // pas 0x49 ...
		{
			readByte(fd); readByte(fd);  // degage les 2 autres char du second cnx/fin (man ou auto)
			while (1)
			{
				dn->killMe = 1;
				sleep(1000);
			}
		}
		res+=tmp;
	}
	else if (res == 0x19 )
	{
		res=readByte(fd);
		switch (res)
		{
			case 0x41:
			case 0x42:
			case 0x43:
			case 0x48:
				res='e';
				break;
			case 0x4B:
				res='c';
				break;
			default:
				res='*';
		}
	}
	else
	{
		//printf("getKey default %02x\n",res);
	}

	if ( (res < 0x20) || (res == 0x7F) )
		res='*';

	return((unsigned short)res);
}

int getvdt(char * buffer, char * name)
{
	FILE * filep;
	int len;
	char path[256]="pages/";

	strcat(path,name);

	filep = fopen (path, "r");
	if (filep == NULL)
	{
		perror(name);
		exit(3);
	}
	len = fread(buffer, 1, BUFFSIZE-1, filep);
	fclose(filep);
	buffer[BUFFSIZE-1]=0x00;
	return(len);
}

int getFile(char * buffer, char * name)
{
	FILE * filep;
	int len;

	filep = fopen (name, "r");
	if (filep == NULL)
	{
		perror(name);
		exit(3);
	}
	len = fread(buffer, 1, BUFFSIZE-1, filep);
	fclose(filep);
	buffer[BUFFSIZE-1]=0x00;
	return(len);
}

// diverses chaines statiques du Minitel
char videotext[]   = { 0x1B, 0x5B, 0x3F, 0x7B };
char echoOff[]     = { 0x1B, 0x3B, 0x60, 0x58, 0x52 };
char kbdEtendu[]   = { 0x1B, 0x3B, 0x69, 0x59, 0x41 };
char kbdNEtendu[]  = { 0x1B, 0x3B, 0x6A, 0x59, 0x41 };
char kbdCurseur[]  = { 0x1B, 0x3B, 0x6A, 0x59, 0x43 };
char kbdNCurseur[] = { 0x1B, 0x3B, 0x6A, 0x59, 0x43 };
char kbdMin[]      = { 0x1B, 0x3A, 0x69, 0x45 };
char kbdMaj[]      = { 0x1B, 0x3A, 0x6A, 0x45 };
char effTout[]     = { 0x0C, 0x1F, 0x40, 0x41, 0x18, 0x0A };
char effLigne[]    = { 0x1B, 0x5B, 0x32, 0x4B };

void setVideotext(int fd)
{
	sendBuf(fd,videotext,strlen(videotext));
	//sleep(1);
	usleep(300000);
	discardByte(fd,5);
}

void setEchoOff(int fd)
{
	sendBuf(fd,echoOff,strlen(echoOff));
	//sleep(1);
	usleep(300000);
	discardByte(fd,5);
}

void setkbdEtendu(int fd)
{
	sendBuf(fd,kbdEtendu,strlen(kbdEtendu));
	//sleep(1);
	usleep(300000);
	discardByte(fd,5);
}

void setkbdEtenduD(int fd)
{
	sendBuf(fd,kbdEtendu,strlen(kbdEtendu));
	//sleep(1);
	usleep(300000);
	//discardByte(fd,5);
}

void setkbdNEtendu(int fd)
{
	sendBuf(fd,kbdNEtendu,strlen(kbdNEtendu));
	//sleep(1);
	usleep(300000);
	discardByte(fd,5);
}

void setkbdCurseur(int fd)
{
	sendBuf(fd,kbdCurseur,strlen(kbdCurseur));
	//sleep(1);
	usleep(300000);
	discardByte(fd,5);
}

void setkbdNCurseur(int fd)
{
	sendBuf(fd,kbdNCurseur,strlen(kbdNCurseur));
	//sleep(1);
	usleep(300000);
	discardByte(fd,5);
}

void setkbdNCurseurD(int fd)
{
	sendBuf(fd,kbdNCurseur,strlen(kbdNCurseur));
	//sleep(1);
	usleep(300000);
	//discardByte(fd,5);
}

void setkbdMin(int fd)
{
	sendBuf(fd,kbdMin,strlen(kbdMin));
	//sleep(1);
	usleep(300000);
	discardByte(fd,4);
}

void setkbdMaj(int fd)
{
	sendBuf(fd,kbdMaj,strlen(kbdMaj));
	//sleep(1);
	usleep(300000);
	discardByte(fd,4);
}

void efface(int fd)
{
	sendByte(fd,0x0C);
}

void effaceLigne(int fd)
{
	sendBuf(fd,effLigne,strlen(effLigne));
}

void effaceTout(int fd)
{
	sendBuf(fd,effTout,strlen(effTout));
}

void bip(int fd)
{
	sendByte(fd,0x07);
}

void curseurOn(int fd)
{
	sendByte(fd,0x11);
}

void curseurOff(int fd)
{
	sendByte(fd,0x14);
}

void repetition(int fd,char car, int nbr)
{
	sendByte(fd,car);
	sendByte(fd,0x12);
	sendByte(fd,0x40+nbr-1);
}

void invVidOn(int fd)
{
	sendByte(fd,0x1B);
	sendByte(fd,0x5D);
}

void invVidOff(int fd)
{
	sendByte(fd,0x1B);
	sendByte(fd,0x5C);
}

void position(int fd, int colonne, int ligne)
{
	sendByte(fd,0x1F);
	sendByte(fd,0x40+ligne);
	sendByte(fd,0x40+colonne);
}

// change les couleurs en niveaux de gris progressifs de 0 - 7
char correspondance[8] = {0x0, 0x4, 0x1, 0x5, 0x2, 0x6, 0x3, 0x7};
void couleur(int fd, int couleur)
{
	sendByte(fd,0x1B);
	sendByte(fd,0x40 + correspondance[couleur]);
}

void couleurFond(int fd, int couleurFond)
{
	sendByte(fd,0x1B);
	sendByte(fd,0x50 + correspondance[couleurFond]);
}

void taille(int fd, int hauteur, int largeur)
{
	sendByte(fd,0x1B);
	sendByte(fd,0x4C + hauteur + largeur*2);
}

void clignotementOn(int fd)
{
	sendByte(fd,0x1B);
	sendByte(fd,0x48);
}

void clignotementOff(int fd)
{
	sendByte(fd,0x1B);
	sendByte(fd,0x49);
}

void affiche(dataNode * dn)
{
	char val[1024];
	char affichage[1024];
	int i;

	curseurOff(dn->fdSP);

	position(dn->fdSP,dn->posx,dn->posy);
	couleur(dn->fdSP,dn->couleur);
	if (dn->champ_cache)
	{
		for ( i=0 ; i<strlen(dn->valeur) ; i++)
			val[i]='*';
		val[i]=0x0;
	}
	else
		strcpy(val,dn->valeur);

	if ( (strlen(val) - dn->decalage) <= dn->longueur_visible )
	{	// plus petit ajoute des ...
		strcpy(affichage,&val[dn->decalage]);
		for ( i=0 ; i < (dn->longueur_visible - (strlen(val) - dn->decalage)) ; i++ )
			affichage[strlen(dn->valeur)-dn->decalage+i]='.';
		affichage[strlen(dn->valeur)-dn->decalage+i]=0x0;
	}
	else
	{
		strncpy(affichage,&val[dn->decalage],dn->longueur_visible);
		affichage[dn->longueur_visible]=0x0;
	}
	sendStr(dn->fdSP,affichage);
	position(dn->fdSP,dn->posx+dn->curseur_x-dn->decalage,dn->posy);
	curseurOn(dn->fdSP);
	// printf("affichage: %s\n",affichage);
}

void gereArrivee(dataNode * dn)
{
	position(dn->fdSP,dn->posx+dn->curseur_x-dn->decalage,dn->posy);
}

int curseurGauche(dataNode * dn)
{

	if (dn->curseur_x == 0 )
	{ //dernier char
		bip(dn->fdSP);
		return(0);
	}
	else
	{
		dn->curseur_x--;
		
		if ( dn->curseur_x < dn->decalage )
		{
			dn->decalage = max(0,dn->decalage - dn->longueur_visible / 2);
			affiche(dn);
		}
		else
		{
			position(dn->fdSP,dn->posx+dn->curseur_x-dn->decalage,dn->posy);
		}
	return(1);
	}

}

int curseurDroite(dataNode * dn)
{

	if (dn->curseur_x == min(strlen(dn->valeur),dn->longueur_totale) )
	{ //dernier char
		bip(dn->fdSP);
		return(0);
	}
	else
	{
		dn->curseur_x++;
		
		if ( dn->curseur_x > (dn->decalage + dn->longueur_visible) )
		{
			dn->decalage = max(0,dn->decalage + dn->longueur_visible / 2);
			affiche(dn);	// a voir
		}
		else
		{
			position(dn->fdSP,dn->posx+dn->curseur_x-dn->decalage,dn->posy);
		}
		return(1);
	}

}

void initChamp(dataNode * dn, int posx, int posy, int longueur_visible, int longueur_totale, char * valeur, int couleur, int champ_cache)
{
	dn->posx = posx;
	dn->posy = posy;
	dn->largeur = longueur_visible;
	dn->hauteur = 1;
	dn->couleur = couleur;
	dn->longueur_visible = longueur_visible;
	dn->longueur_totale = longueur_totale;
	strcpy(dn->valeur,valeur);
	dn->curseur_x = dn->decalage = dn->accent = 0;
	dn->activable = 1;
	dn->champ_cache = champ_cache;
	curseurOn(dn->fdSP);
}

void saisie(dataNode * dn, char * res, int posx, int posy, int longueur_visible, int longueur_totale, char * valeur, int couleur, int champ_cache)
{
	unsigned short key;
	char special,car;
	char val[1024];
	
	flushInput(dn->fdSP);

	initChamp(dn,posx,posy,longueur_visible,longueur_totale,valeur,couleur,champ_cache);

	affiche(dn);
	gereArrivee(dn);

	while(1)
	{
		key = getKey(dn);
		car = (char)(key & 0xFF);
		//printf("saisie: %02x\n",car);
		if ( key <= 127 )
		{ // simple car
			if (strlen(dn->valeur) >= dn->longueur_totale)
			{
				bip(dn->fdSP);
			}
			else
			{
				strncpy(val,dn->valeur,dn->curseur_x);
				val[dn->curseur_x]=car;
				val[dn->curseur_x+1]=0x0;
				strcat(val,&dn->valeur[dn->curseur_x]);
				strcpy(dn->valeur,val);
				
				curseurDroite(dn);
				affiche(dn);
			}
		}
		else
		{ // touche étendue
			special = ((char)(key>>8)&0xFF);
			//printf("etendue: %02x\n",special);
			if (special == 0x5B)
			{
				if (car == 0x44)
				{ // gauche
					curseurGauche(dn);
				}
				else if (car == 0x43)
				{ // droite
					curseurDroite(dn);
				}
			}
			else if (special == 0x13)
			{
				if (car == 0x47)
				{ // correction
					if (curseurGauche(dn))
					{
						strncpy(val,dn->valeur,dn->curseur_x);
						val[dn->curseur_x]=0x0;
						strcat(val,&dn->valeur[dn->curseur_x+1]);
						strcpy(dn->valeur,val);
						affiche(dn);
					}
				}
				else if (car == 0x41)
				{ // envoie
					if (!dn->valeur[0])
						bip(dn->fdSP);	// pas de chaine null possible avec "Envoi"
					else
						break;
				}
				else if (car == 0x45)
				{ // annulation
					bip(dn->fdSP);
					dn->valeur[0]=0x0;
					break;
				}
				else if (car == 0x42)
				{ // retour
					curseurGauche(dn);
				}
				else if (car == 0x48)
				{ // suite
					curseurDroite(dn);
				}
				else if (car == 0x44)
				{ // guide
					strcpy(dn->valeur,"Guide\xFE");
					break;
				}
				else if (car == 0x46)
				{ // sommaire
					strcpy(dn->valeur,"Sommaire\xFE");
					break;
				}
			}
		}
		//affiche(dn);
	}
	strcpy(res,dn->valeur);
}

void passwdGrehack(dataNode * dn)
{
	// logo Grehack ascii
/*	position(dn->fdSP,10,2);
	taille(dn->fdSP,1,1);
	couleur(dn->fdSP,2);
	sendStr(dn->fdSP,"____________\0");
	position(dn->fdSP,8,4);
	taille(dn->fdSP,1,1);
	couleur(dn->fdSP,2);
	sendStr(dn->fdSP,"}GREHACK 1990{\0");
	position(dn->fdSP,10,5);
	taille(dn->fdSP,0,1);
	couleur(dn->fdSP,2);
	sendStr(dn->fdSP,"~~~~~~~~~~~~\0"); */

	// plus joli :
	curseurOff(dn->fdSP);
	getvdt(dn->buffer1,"GrehackPlein.vdt");
	sendStr(dn->fdSP,dn->buffer1);

	position(dn->fdSP,10,12);
	couleur(dn->fdSP,2);
	sendStr(dn->fdSP,"Mot de passe : ");
	position(dn->fdSP,25,13);
	couleur(dn->fdSP,2);
	sendStr(dn->fdSP,"~~~~");

	while (1)
	{
		// password
	
		saisie(dn,dn->buffer1,25,12,4,4,"",7,1);
		printf("Node %d / Mot de passe : ->%s<-\n",dn->num,dn->buffer1);
	
		strlwr(dn->buffer1);
		if (!strcmp(dn->buffer1,"bnd2"))
			break;
	}

	curseurOff(dn->fdSP);
	sleep(1);
	effaceLigne(dn->fdSP);
	position(dn->fdSP,25,13);
	effaceLigne(dn->fdSP);
	sleep(2);
	effaceTout(dn->fdSP);

	getvdt(dn->buffer1,"flag1.vdt");
	sendStr(dn->fdSP,dn->buffer1);
	getKey(dn);

}

int menuTeletel(dataNode * dn)
{
	int len1,len2,key;
	len1 = getvdt(dn->buffer1,"teletel3.vdt");
	len2 = getvdt(dn->buffer2,"annuaire.vdt");

	while (1)
	{
		curseurOff(dn->fdSP);
		sendBuf(dn->fdSP,dn->buffer1,len1);	// 3615
		saisie(dn,dn->buffer3,12,17,28,50,"",7,0);
		printf("Node %d / Service : ->%s<-\n",dn->num,dn->buffer3);
	
		strlwr(dn->buffer3);
		effaceTout(dn->fdSP);
		
		if (!strcmp(dn->buffer3,"guide\xFE")) 
		{
			sendBuf(dn->fdSP,dn->buffer2,len2);
			//
			position(dn->fdSP,1,8);
			couleur(dn->fdSP,7);
			sleep(3);
			sleep(3);
			sendByte(dn->fdSP,'.');
			sleep(3);
			sendByte(dn->fdSP,'.');
			sleep(3);
			sendByte(dn->fdSP,'.');
			sleep(3);
			sendByte(dn->fdSP,'.');
			sleep(3);
			sendByte(dn->fdSP,'.');
			clignotementOn(dn->fdSP);
			taille(dn->fdSP,1,0);
			sendStr(dn->fdSP," Temps d'attente d\x19\x42\x65pass\x19\x42\x65.");
			usleep(300000);
			bip(dn->fdSP);
			clignotementOff(dn->fdSP);
			taille(dn->fdSP,0,0);
			sleep(5);
			position(dn->fdSP,1,10);
			sendStr(dn->fdSP,"Connexion au r\x19\x42\x65seau interrompue.");
			sleep(5);
			sendStr(dn->fdSP," Pas de r\x19\x42\x65ponse de Transpac.");
			sleep(5);
			sendStr(dn->fdSP," Le service est-il encore en fonctionnement ?");
			sleep(5);
			position(dn->fdSP,1,14);
			sendStr(dn->fdSP,"Service local disponible :");
			position(dn->fdSP,1,15);
			repetition(dn->fdSP,'`',40);
			position(dn->fdSP,1,16);
			invVidOn(dn->fdSP);
			sendStr(dn->fdSP,"gReTEL");
			position(dn->fdSP,1,17);
			sendStr(dn->fdSP,"Serveur t\x19\x42\x65l\x19\x42\x65matique de l'\x19\x42\x65v\x19\x42\x65nement GREHACK 1990");
			position(dn->fdSP,1,19);
			repetition(dn->fdSP,'`',40);
			position(dn->fdSP,1,21);
			couleurFond(dn->fdSP,0);
			sendStr(dn->fdSP,"Fin.");
			
			position(dn->fdSP,1,24);
			repetition(dn->fdSP,'`',33);
			invVidOn(dn->fdSP);
			sendStr(dn->fdSP,"Retour");
			invVidOff(dn->fdSP);
			sendByte(dn->fdSP,'`');

			while(1)
			{
				key = getKey(dn);
				if ((((char)(key>>8)&0xFF) == 0x13) && (((char)(key & 0xFF))==0x42))
				{ // Retour
					break;
				}
			}
		}
		else if (!strcmp(dn->buffer3,"gretel"))
		{
			return(1);	// gReTEL
		}
		else if (!strcmp(dn->buffer3,"ulla"))
		{
			return(2);	// ulla, malheureusement pas eu le temps d'implémenter un peu de p0rn ...
		}
	}
}

void chargeAD(dataAD * ptr, int len)
{
	FILE* fd;

        fd = fopen( fichierAnnonces, "rb");
        if (fd == NULL)
        {
                perror("Thread-init: Fichier AD impossible à ouvrir\n");
                exit(1);
        }
        fread(ptr,len,1,fd);
        fclose(fd);
}

void ecritAD(dataAD * ptr, char * nom, int nbr)
{
	FILE* fd;

        fd = fopen(nom , "wb+");
        if (fd == NULL)
        {
                perror("Thread-init: Fichier AD impossible à créer\n");
                exit(1);
        }
        fwrite(ptr,sizeof(dataAD),nbr,fd);
        fclose(fd);
}

void afficheAD(dataNode * dn)
{
	int i,key,reste=1;
	dataAD cacheAD[5];

	curseurOff(dn->fdSP);

	do
	{
		effaceTout(dn->fdSP);
		pthread_mutex_lock( &mutexAD );
		for (i=0 ; i<5 && ((dn->posAD+i)<nbrAD) ; i++)
		{
			memcpy(&cacheAD[i],&ptrAD[dn->posAD+i],sizeof(dataAD));
		}
		pthread_mutex_unlock( &mutexAD );
		
		sprintf(dn->buffer1," %d",nbrAD);
		position(dn->fdSP,8,1);
		couleurFond(dn->fdSP,1);
		couleur(dn->fdSP,0);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP,dn->buffer1);
		sendStr(dn->fdSP," Annonces "); 

		sprintf(dn->buffer1,"%d",dn->posAD/5+1);
		position(dn->fdSP,1,2);
		couleur(dn->fdSP,2);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP,"Page "); 
		couleur(dn->fdSP,4);
		sendStr(dn->fdSP,dn->buffer1);
		for (i=0 ; i<5 && ((dn->posAD+i)<nbrAD) ; i++)
		{
			position(dn->fdSP,1,3+i*4);
			repetition(dn->fdSP,'`',40);

			position(dn->fdSP,1,4+i*4);
			couleurFond(dn->fdSP,3);
			couleur(dn->fdSP,6);
			sendByte(dn->fdSP,' ');
			sendStr(dn->fdSP,cacheAD[i].pseudo);
			sendByte(dn->fdSP,' ');

			position(dn->fdSP,14,4+i*4);
			couleur(dn->fdSP,6);
			invVidOn(dn->fdSP);
			sendStr(dn->fdSP,cacheAD[i].titre);

			position(dn->fdSP,1,5+i*4);
			sendStr(dn->fdSP,cacheAD[i].corps);
		}
		position(dn->fdSP,1,3+i*4);
		repetition(dn->fdSP,'`',40);

		position(dn->fdSP,1,24);
		sendByte(dn->fdSP,'`');
		invVidOn(dn->fdSP);
		sendStr(dn->fdSP,"Sommaire");
		invVidOff(dn->fdSP);
		repetition(dn->fdSP,'`',17);
		invVidOn(dn->fdSP);
		sendStr(dn->fdSP,"Retour");
		invVidOff(dn->fdSP);
		sendByte(dn->fdSP,'`');
		invVidOn(dn->fdSP);
		sendStr(dn->fdSP,"Suite");
		invVidOff(dn->fdSP);
		sendByte(dn->fdSP,'`');

		while (1)
		{
			key = getKey(dn);
			if ((((char)(key>>8)&0xFF) == 0x13) && (((char)(key & 0xFF))==0x42))
			{ // retour
				if ( ((dn->posAD-5)>=0) )
				{
					dn->posAD-=5;
					break;
				}
			} 
			else if ((((char)(key>>8)&0xFF) == 0x13) && (((char)(key & 0xFF))==0x48))
			{ // suivant
				if ( !((dn->posAD+5)>=nbrAD) )
				{
					dn->posAD+=5;
					break;
				}
			}
			else if ((((char)(key>>8)&0xFF) == 0x13) && (((char)(key & 0xFF))==0x46))
			{ // sommaire
				return;
			}
		}

	} while (reste);
}

int testInjection(char * str)
{
	// masque de l'injection : .*";cmd;*"*
	char *ptr,*ptr2,*cmd;
	int i;

	if (str[0]=='"')
		return 1;

	ptr = strstr(str,"\";");
	if (ptr == NULL)
		return 0;	// annonce valide

	// ici on a un client, on enregistre plus l'annonce
	if (ptr == str)	// "; au début, inj raté
		return 1;

	ptr+=2;	// saute ";

	cmd=ptr; // nouveau depart

	ptr2 = strchr(ptr,';');
	if (ptr2 == NULL) // pas de ';'
		return 2;
	
	i = (int) ptr2 - (int) cmd;
	if (i < 2)	// cmd d'au moins 2 de long
		return 3;


	ptr2++;	// saute ;

	ptr=ptr2; // nouveau depart

	ptr2 = strchr(ptr,'"');
	if (ptr2 == NULL) // pas de '"'
		return 4;
	
	// allez, ce n'est qu'un chall à 325, on est moins vache
/*	i = (int) ptr2 - (int) ptr;
	if (i < 1)	// padding d'au moins 1 de long
		return 5;

	if (!ptr2[1])	// pas de dernier octet de padding
		return 6;*/

	return (int)cmd;
}

void deposerAD(dataNode * dn)
{
	dataAD newAD;
	int i,key,resInjection;

	effaceTout(dn->fdSP);
	curseurOff(dn->fdSP);

	position(dn->fdSP,1,1);
	couleurFond(dn->fdSP,1);
	couleur(dn->fdSP,0);
	taille(dn->fdSP,0,1);
	sendStr(dn->fdSP," D\x19\x42\x65poser une annonce"); 

	taille(dn->fdSP,0,0);

	position(dn->fdSP,1,4);
	invVidOn(dn->fdSP);
	sendStr(dn->fdSP,"Pseudo :"); 
	invVidOff(dn->fdSP);
	sendByte(dn->fdSP,' ');
	repetition(dn->fdSP,'.',10);

	position(dn->fdSP,1,6);
	invVidOn(dn->fdSP);
	sendStr(dn->fdSP,"Titre  :"); 
	invVidOff(dn->fdSP);
	sendByte(dn->fdSP,' ');
	repetition(dn->fdSP,'.',25);

	position(dn->fdSP,1,8);
	invVidOn(dn->fdSP);
	sendStr(dn->fdSP,"Texte :"); 
	position(dn->fdSP,1,9);
	repetition(dn->fdSP,'.',39);

	position(dn->fdSP,1,24);
	repetition(dn->fdSP,'`',22);
	invVidOn(dn->fdSP);
	sendStr(dn->fdSP,"Annulation");
	invVidOff(dn->fdSP);
	sendByte(dn->fdSP,'`');
	invVidOn(dn->fdSP);
	sendStr(dn->fdSP,"Envoi");
	invVidOff(dn->fdSP);
	sendByte(dn->fdSP,'`');

	saisie(dn,dn->buffer1,10,4,10,10,"",7,0);
	if (dn->buffer1[0] == 0x0)
		return; // touche annulation
	strcpy(newAD.pseudo,dn->buffer1);

	saisie(dn,dn->buffer1,10,6,25,25,"",7,0);
	if (dn->buffer1[0] == 0x0)
		return; // touche annulation
	strcpy(newAD.titre,dn->buffer1);

	saisie(dn,dn->buffer1,1,9,39,80,"",7,0);
	if (dn->buffer1[0] == 0x0)
		return; // touche annulation
	strcpy(newAD.corps,dn->buffer1);


	
	// affiche l'annonce saisie, ici trou de sécu volontaire

	effaceTout(dn->fdSP);
	curseurOff(dn->fdSP);

	position(dn->fdSP,1,1);
	couleurFond(dn->fdSP,1);
	couleur(dn->fdSP,0);
	taille(dn->fdSP,0,1);
	sendStr(dn->fdSP," D\x19\x42\x65poser une annonce"); 

	position(dn->fdSP,1,3);
	sendStr(dn->fdSP,"Votre annonce :");


	position(dn->fdSP,1,3+1*4);
	repetition(dn->fdSP,'`',40);

	position(dn->fdSP,1,4+1*4);
	couleurFond(dn->fdSP,3);
	couleur(dn->fdSP,6);
	sendByte(dn->fdSP,' ');
	sendStr(dn->fdSP,newAD.pseudo);
	sendByte(dn->fdSP,' ');

	position(dn->fdSP,14,4+1*4);
	couleur(dn->fdSP,6);
	invVidOn(dn->fdSP);
	sendStr(dn->fdSP,newAD.titre);

	position(dn->fdSP,1,5+1*4);
	sendStr(dn->fdSP,newAD.corps);

	position(dn->fdSP,1,3+2*4);
	repetition(dn->fdSP,'`',40);
	
	position(dn->fdSP,1,24);
	repetition(dn->fdSP,'`',22);
	invVidOn(dn->fdSP);
	sendStr(dn->fdSP,"Annulation");
	invVidOff(dn->fdSP);
	sendByte(dn->fdSP,'`');
	invVidOn(dn->fdSP);
	sendStr(dn->fdSP,"Envoi");
	invVidOff(dn->fdSP);
	sendByte(dn->fdSP,'`');

	while (1)
	{
		key = getKey(dn);
		if ((((char)(key>>8)&0xFF) == 0x13) && (((char)(key & 0xFF))==0x45))
		{ // Annulation
			return;
		} 
		else if ((((char)(key>>8)&0xFF) == 0x13) && (((char)(key & 0xFF))==0x41))
		{
			// test trou de secu (ne pas enregistrer d'annonce avec "; pour ne pas donner d'indice)
			resInjection = testInjection(newAD.corps);
			if ( resInjection )
			{
				// on a un malin
				printf("Node %d / Injection essayée : ->%s<-\n",dn->num,newAD.corps);
				effaceTout(dn->fdSP);
				position(dn->fdSP,3,12);
				couleurFond(dn->fdSP,5);
				couleur(dn->fdSP,1);
				if (resInjection == 1)
				{
					sendStr(dn->fdSP," ERREUR : chaine vide");
				}
				else if (resInjection == 2)
				{
					sendStr(dn->fdSP," -bash:\x0A commande introuvable");
				}
				else if (resInjection == 3)
				{
					sendStr(dn->fdSP," -bash:\x0A commande malformée");
				}
				else if (resInjection == 4)
				{
					sendStr(dn->fdSP," ERREUR : parsing, char \" manquant");
				}
				else
				{ // check de la commande
					if (strstr((char *)resInjection,"ls"))
					{
						sendStr(dn->fdSP," annonces.db  annonces.vdt  flag1.vdt  flag2.vdt  GrehackBrique.vdt  GrehackTag.vdt  teletel3.vdt\x0A annuaire.vdt  flag.txt  GrehackPlein.vdt   gre-tel1.vdt    testEcran.vdt");
					}
					else if (strstr((char *)resInjection,"cat flag.txt"))
					{
						printf("Node %d / Injection réussie : ->%s<-\n",dn->num,newAD.corps);
						sendStr(dn->fdSP," Bien jou\x19\x42\x65! Flag=MinitelMaster!");
					}
					else if (strstr((char *)resInjection,"cat "))
					{
						sendStr(dn->fdSP," @@@@%^^");
						bip(dn->fdSP);
						sendStr(dn->fdSP,"Segmentation fault");
					}
					else if(strstr((char *)resInjection,"pwd"))
					{
						sendStr(dn->fdSP," /home/pi/Grehack2015/Minitel/Serveur/datas");
					}
					else if(strstr((char *)resInjection,"id"))
					{
						sendStr(dn->fdSP," uid=1000(pi) gid=1000(pi) groupes=1000(pi),4(adm),20(dialout),24(cdrom),27(sudo),29(audio),44(video),46(plugdev),60(games),100(users),101(input),108(netdev),997(gpio),998(i2c),999(spi)");
					}
					else if(strstr((char *)resInjection,"uname"))
					{
						sendStr(dn->fdSP," Linux minitel 4.1.7-v7+ @@%^{{{");
						bip(dn->fdSP);
						sendStr(dn->fdSP,"Segmentation fault");
					}
					else if(strstr((char *)resInjection,"env"))
					{
						sendStr(dn->fdSP," XDG_SESSION_ID=c18\x0ATERM=xterm\xASHELL=/bin/sh\xAUSER=pi\xALS_COLORS=rs=0@@%^}}}");
						bip(dn->fdSP);
						sendStr(dn->fdSP,"Segmentation fault");
					}
					else
					{
						sprintf(dn->buffer1,"%s",(char *)resInjection);
						sendStr(dn->fdSP," -bash:"); 
						sendStr(dn->fdSP,dn->buffer1);
						sendStr(dn->fdSP," commande introuvable");
					}
				}

				getKey(dn);
				return;
			}
			else
			{	// annonce normal à enregistrer
				pthread_mutex_lock( &mutexAD );

			        if ((ptr2AD=calloc(nbrAD+1,sizeof(dataAD))) == NULL )
			        {
			                perror("Thread-Node: outOfMemory sur les ADs !\n");
			                exit(3);
			        }
				memset(ptr2AD,0,(nbrAD+1)*sizeof(dataAD));
				for (i=0 ; i<nbrAD ; i++)
					memcpy(&ptr2AD[i+1],&ptrAD[i],sizeof(dataAD));
				
				memcpy(&ptr2AD[0],&newAD,sizeof(dataAD));

				nbrAD++;

				ecritAD(ptr2AD,fichierAnnonces,nbrAD);

				free(ptrAD);

				ptrAD=ptr2AD;
				
				pthread_mutex_unlock( &mutexAD );

				// ecran confirmation dépot
				effaceTout(dn->fdSP);
				position(dn->fdSP,3,12);
				couleurFond(dn->fdSP,5);
				couleur(dn->fdSP,0);
				sendStr(dn->fdSP," Votre annonce a bien \x19\x42\x65t\x19\x42\x65 enregistr\x19\x42\x65");
				sendByte(dn->fdSP,'e');
				getKey(dn);
				return;
			}
		}
	}
}

void AD(dataNode * dn)
{
	int len1,reste=1;

	while (1)
	{
		curseurOff(dn->fdSP);
		len1 = getvdt(dn->buffer1,"annonces.vdt");
		sendByte(dn->fdSP,0x0C);
		sendBuf(dn->fdSP,dn->buffer1,len1);

		position(dn->fdSP,3,12);
		couleurFond(dn->fdSP,4);
		couleur(dn->fdSP,0);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP," 1 ");
		taille(dn->fdSP,0,0);
		couleurFond(dn->fdSP,0);
		sendByte(dn->fdSP,' ');
		couleurFond(dn->fdSP,4);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP," Consulter  ");
	
		position(dn->fdSP,3,13);
		couleurFond(dn->fdSP,5);
		couleur(dn->fdSP,0);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP," 2 ");
		taille(dn->fdSP,0,0);
		couleurFond(dn->fdSP,0);
		sendByte(dn->fdSP,' ');
		couleurFond(dn->fdSP,5);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP," Deposer    ");
	
		position(dn->fdSP,3,14);
		couleurFond(dn->fdSP,6);
		couleur(dn->fdSP,0);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP," 0 ");
		taille(dn->fdSP,0,0);
		couleurFond(dn->fdSP,0);
		sendByte(dn->fdSP,' ');
		couleurFond(dn->fdSP,6);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP," Retour     ");
	
		position(dn->fdSP,3,16);
		couleurFond(dn->fdSP,7);
		couleur(dn->fdSP,0);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP," Choix : ");

		do
		{
			saisie(dn,dn->buffer2,22,16,1,1,"",7,0);
			switch (dn->buffer2[0])
			{
				case '1':
					afficheAD(dn);
					reste=0;
				break;
	
				case '2':
					deposerAD(dn);
					reste=0;
				break;
	
				case '0':
					return;
				break;
			}
		} while (reste);
	}

}

int jeuxAff(dataNode * dn, char * fich, char * nom)
{
	int len ;

	efface(dn->fdSP);
	curseurOff(dn->fdSP);
	len = getvdt(dn->buffer2,fich);
	sendBuf(dn->fdSP,dn->buffer2,len);

	position(dn->fdSP,1,24);
	sendStr(dn->fdSP,"Qui est-ce ?");
	position(dn->fdSP,32,24);
	invVidOn(dn->fdSP);
	sendStr(dn->fdSP,"Sommaire");

	while (1)
	{
		saisie(dn,dn->buffer1,14,24,15,30,"",7,0);
		strlwr(dn->buffer1);
		printf("Node %d / Jeux : ->%s<-\n",dn->num,dn->buffer1);
		if (!strcmp(dn->buffer1,"sommaire\xFE"))
			return 1;
		if (!strcmp(dn->buffer1,nom))
			return 0;
	}
}

void jeux(dataNode * dn)
{
	int len;

	if (jeuxAff(dn,"assange.vdt","assange"))
		return;
	if (jeuxAff(dn,"stallman.vdt","stallman"))
		return;
	if (jeuxAff(dn,"norris.vdt","norris"))
		return;
	if (jeuxAff(dn,"gates.vdt","gates"))
		return;
	if (jeuxAff(dn,"guevara.vdt","guevara"))
		return;
	if (jeuxAff(dn,"swartz.vdt","swartz"))
		return;
	if (jeuxAff(dn,"snowden.vdt","snowden"))
		return;
	if (jeuxAff(dn,"lagaffe.vdt","lagaffe"))
		return;
	if (jeuxAff(dn,"brown.vdt","brown"))
		return;

	curseurOff(dn->fdSP);
	len = getvdt(dn->buffer2,"flag2.vdt");
	sendBuf(dn->fdSP,dn->buffer2,len);
	getKey(dn);
}

int dispPix(dataNode * dn, char * fichier)
{
	int len, key ;

	len = getvdt(dn->buffer1,fichier);
	sendBuf(dn->fdSP,dn->buffer1,len);
	key = getKey(dn);
	if ((((char)(key>>8)&0xFF) == 0x13) && (((char)(key & 0xFF))==0x46))
		return 1;
	return 0;

}

void pixArt(dataNode * dn)
{
	effaceTout(dn->fdSP);
	curseurOff(dn->fdSP);

	if ( dispPix(dn,"pix/GrehackBrique.vdt") )
		return;
	if ( dispPix(dn,"pix/GrehackTag.vdt") )
		return;
	if ( dispPix(dn,"pix/minitel.vdt") )
		return;
	if ( dispPix(dn,"pix/blackhat.vdt") )
		return;
	if ( dispPix(dn,"pix/route66.vdt") )
		return;
	if ( dispPix(dn,"pix/fighter.vdt") )
		return;
	if ( dispPix(dn,"pix/delta.vdt") )
		return;
	if ( dispPix(dn,"pix/surfeur.vdt") )
		return;
	if ( dispPix(dn,"pix/astronaute.vdt") )
		return;
}

void gretel(dataNode * dn)
{
	int len1,reste=1;
	
	effaceTout(dn->fdSP);
	curseurOff(dn->fdSP);
	len1 = getvdt(dn->buffer1,"fantome.vdt");
	sendBuf(dn->fdSP,dn->buffer1,len1);

	position(dn->fdSP,27,5);
	couleurFond(dn->fdSP,7);
	couleur(dn->fdSP,0);
	clignotementOn(dn->fdSP);
	//invVidOn(dn->fdSP);
	taille(dn->fdSP,1,0);
	sendStr(dn->fdSP," BIENVENUE !");
	getKey(dn);

	while(1)
	{
		effaceTout(dn->fdSP);
		curseurOff(dn->fdSP);
		len1 = getvdt(dn->buffer1,"gre-tel1.vdt");
		sendByte(dn->fdSP,0x0C);

		repetition(dn->fdSP,'_',40);
		sendBuf(dn->fdSP,dn->buffer1,len1);
		position(dn->fdSP,1,10);
		repetition(dn->fdSP,'`',40);

		position(dn->fdSP,3,12);
		couleurFond(dn->fdSP,4);
		couleur(dn->fdSP,0);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP," 1 ");
		taille(dn->fdSP,0,0);
		couleurFond(dn->fdSP,0);
		sendByte(dn->fdSP,' ');
		couleurFond(dn->fdSP,4);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP," Annonces   ");

		position(dn->fdSP,3,13);
		couleurFond(dn->fdSP,5);
		couleur(dn->fdSP,0);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP," 2 ");
		taille(dn->fdSP,0,0);
		couleurFond(dn->fdSP,0);
		sendByte(dn->fdSP,' ');
		couleurFond(dn->fdSP,5);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP," Jeux       ");

		position(dn->fdSP,3,14);
		couleurFond(dn->fdSP,6);
		couleur(dn->fdSP,0);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP," 3 ");
		taille(dn->fdSP,0,0);
		couleurFond(dn->fdSP,0);
		sendByte(dn->fdSP,' ');
		couleurFond(dn->fdSP,6);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP," Pix-Art    ");

		position(dn->fdSP,3,16);
		couleurFond(dn->fdSP,7);
		couleur(dn->fdSP,0);
		taille(dn->fdSP,0,1);
		sendStr(dn->fdSP," Choix : ");
		
		position(dn->fdSP,1,24);
		couleur(dn->fdSP,1);
		sendStr(dn->fdSP,"(C) Grehack, 1990. Serveur T\x19\x42\x65l\x19\x42\x65tel V0.9");
		do
		{
			saisie(dn,dn->buffer2,22,16,1,1,"",7,0);
			switch (dn->buffer2[0])
			{
				case '1':
					AD(dn);
					reste=0;
				break;

				case '2':
					jeux(dn);
					reste=0;
				break;

				case '3':
					pixArt(dn);
					reste=0;
				break;
			}
		} while (reste);
	}
}

void initMinitelBascul(dataNode * dn)
{
	printf("Init Minitel node num %d\n",dn->num);
	printf("setkbdEtendu\n");
	setkbdEtenduD(dn->fdSP);
	printf("setkbdNCurseur\n");
	setkbdNCurseurD(dn->fdSP);
	printf("setkbdMin\n");
	setkbdMin(dn->fdSP);
	discardByte(dn->fdSP,4);	// pas compris celle là ...
	printf("setEchoOff\n");
	setEchoOff(dn->fdSP);

	printf("setVideotext\n");
	setVideotext(dn->fdSP);

	curseurOff(dn->fdSP);
}

void initMinitelCouliss(dataNode * dn)
{
	printf("Init Minitel node num %d\n",dn->num);
	printf("setkbdEtendu\n");
	setkbdEtendu(dn->fdSP);
	printf("setkbdNCurseur\n");
	setkbdNCurseur(dn->fdSP);
	printf("setkbdMin\n");
	setkbdMin(dn->fdSP);
	discardByte(dn->fdSP,4);	// pas compris celle là ...
	printf("setEchoOff\n");
	setEchoOff(dn->fdSP);

	printf("setVideotext\n");
	setVideotext(dn->fdSP);

	curseurOff(dn->fdSP);
}

void * mainThread(void * param)
{
	dataNode * dn;
	dn = (dataNode *) param;
	int ret;

	//int len;

	printf("Demarrage node num %d\n",dn->num);

	//initMinitel(dn->fdSP); // remplacé par ci-dessous
	switch (dn->num)
	{
		case 0 :
			initMinitelBascul(dn);
			break;
		case 1 :
			initMinitelBascul(dn);
			break;
		case 2 :
			initMinitelCouliss(dn);
			break;
		case 3 :
			initMinitelCouliss(dn);
			break;
		case 4 :
			initMinitelBascul(dn);
			break;
		default : 
			break;
	}

	effaceTout(dn->fdSP);


	// on est parti pour la navigation


/*			position(dn->fdSP,5,8);
			taille(dn->fdSP,1,1);
			sendStr(dn->fdSP,"TEST !!!");
			couleur(dn->fdSP,4);
			sendStr(dn->fdSP," NODE ");
			sendByte(dn->fdSP,'0'+dn->num);
			getKey(dn); */

	// test char :
	/*effaceTout(dn->fdSP);
	position(dn->fdSP,5,8);
	sendStr(dn->fdSP,"TEST.");
	sendByte(dn->fdSP,0xA); //OK !
	sendStr(dn->fdSP,"test2");
	sendByte(dn->fdSP,0xD);
	sendStr(dn->fdSP,"test3");
	getKey(dn);*/

	// accueil, passwd, qrcode flag1
	passwdGrehack(dn);

	ret = menuTeletel(dn);

	if ( ret == 1 )
	{ // gretel
		gretel(dn);
	}
	/*else if (ret == 2)
	{ // ulla

	}*/

	printf("Fin node num %d\n",dn->num);
	dn->killMe=1;
	while (1)
		sleep(1000);
	return(NULL);



	//saisie(dn,dn->buffer1,10,10,20,60,"Hello world",7,0);
	//printf("Chaine saisie (%d) : %s\n",strlen(dn->buffer1),dn->buffer1);

	//printf("%02x\n",readByte(dn->fdSP));
	// tcdrain() -> a voir

}

void createThreads()
{
	int ret,i;
	printf ("Creation des threads.\n");
	for ( i=0 ; i<NODE; i++)
	{
		zeroDN(&dnn[i]);
		dnn[i].num=i;
		ret = pthread_create( &(dnn[i].thread), NULL, mainThread, (void *) &dnn[i]);
					              
		if (ret)
		{
			sprintf(dnn[i].buffer1,"thread num %d\n",ret);
			perror(dnn[i].buffer1);
			exit(2);
		}
	}

	while(1)
	{
		sleep(1);

		// vérifie si une thread doit être redémarrée
		for ( i=0 ; i<NODE; i++)
		{
			if (dnn[i].killMe)
			{
				pthread_cancel ((dnn[i].thread));
				printf("Thread node num %d tuee.\n",dnn[i].num);
				zeroDN(&dnn[i]);
				dnn[i].num=i;
				ret = pthread_create( &(dnn[i].thread), NULL, mainThread, (void *) &dnn[i]);
				if (ret)
				{
					sprintf(dnn[i].buffer1,"thread num %d\n",ret);
					perror(dnn[i].buffer1);
					exit(2);
				}
				printf("Thread node num %d cree.\n",dnn[i].num);
			}
		}

		// ici : autres traitement depuis la thread principale de controle
	}

/*	for ( i=0 ; i<NODE; i++) {
		printf("Debut join %d\n",i);
		pthread_join ((dnn[i].thread), NULL);
		printf("Fin join %d\n",i);
	}
	for ( i=0 ; i<NODE; i++) {
		printf("Debut kill %d\n",i);
		pthread_cancel ((dnn[i].thread));
		printf("Fin kill %d\n",i);
	}*/
}


void initAD()
{
	lenFichAD = lenFile(fichierAnnonces);
	nbrAD = lenFichAD / sizeof(dataAD);
	printf("Thread-init: Nombre d'annonces : %d.\n",nbrAD);

	// Alloc la RAM pour les annonces
	if ((ptrAD=calloc(nbrAD,sizeof(dataAD))) == NULL )
	{
		perror("Thread-init: Fichier AD trop gros ou bizarre, remettre le fichier propre et relancer.\n");
		exit(3);
	}
	chargeAD(ptrAD,lenFichAD);

        rename(fichierAnnonces,fichierAnnoncesOld);
        ecritAD(ptrAD,fichierAnnoncesTmp,nbrAD);
        ecritAD(ptrAD,fichierAnnonces,nbrAD);
}

int main() 
{
	// initialise les ports séries
	initSerialPorts(NODE);

	// charge les annonces
	initAD();

	// envoie la sauce
	createThreads();
	return(0);
}
