#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <direct.h>
#include <time.h>
#include <sys/utime.h>
#include "resource.h"

#include "zlib.h"

#define FUDGEFACTOR 0x14

// 'HAPI;
#define HEX_HAPI 0x49504148
// 'BANK'
#define HEX_BANK 0x4B4E4142
// 'SQSH'
#define HEX_SQSH 0x48535153

#define FALSE 0
#define TRUE 1
#define MAXPATH 255

#pragma pack(1)

typedef struct _HPIVERSION {
  long HPIMarker;              /* 'HAPI' */
  long Version;                /* 'BANK' if savegame, 0x00010000, or 0x000200 */
} HPIVERSION;

typedef struct _HPIHEADER1 {
  long DirectorySize;                /* Directory size */
  long Key;                    /* decode key */
  long Start;                  /* offset of directory */
} HPIHEADER1;

typedef struct _HPIENTRY {
  int NameOffset;
  int CountOffset;
  char Flag;
} HPIENTRY;

typedef struct _HPIHEADER2 {
  long DirBlock;
  long DirSize;
  long NameBlock;
  long NameSize;
  long Data;
  long Last78;
} HPIHEADER2;

typedef struct _HPIDIR2 {
  long NamePtr;
  long FirstSubDir;
  long SubCount;
  long FirstFile;
  long FileCount;
} HPIDIR2;

typedef struct _HPIENTRY2 {
  long NamePtr;
  long Start;
  long DecompressedSize;
  long CompressedSize; /* 0 = no compression */
  long Date;  /* date in time_t format */
  long Checksum;
} HPIENTRY2;

typedef struct _HPICHUNK {
  long Marker;            /* always 0x48535153 (SQSH) */
  char Unknown1;          /* I have no idea what these mean */
	char CompMethod;				/* 1 = lz77, 2 = zlib */ 
	char Encrypt;						/* chunk encrypted? */
  long CompressedSize;    /* the length of the compressed data */
  long DecompressedSize;  /* the length of the decompressed data */
  long Checksum;          /* checksum */
} HPICHUNK;

#pragma pack()

HPIVERSION hv;
long Key;
char *Directory;
long CurPos;
int chunkno = 0;
FILE *HPIFile;
int TotalLength = 0;
int debug = FALSE;
char **OutSpec;

char HPIName[MAXPATH] = "";
char OutDir[MAXPATH] = ".";

/*****************************************************************
 * smatch: Given a data string and a pattern containing one or
 * more embedded stars (*) (which match any number of characters)
 * return true if the match succeeds, and set res[i] to the
 * characters matched by the 'i'th *.
 *
 * borrowed from maas neotek bot code
 * added by Joe D:
 * now not case sensitive
 * '?' match-single-character wildcard.
 * precede *, ? or \ in pat with a \ if you want to match those chars.
 * res can now be NULL if you don't care about the matching chars
 *****************************************************************/

int StarMatch(char *dat, char *pat, char *res[])
{ 
  char *star = NULL;
  char *starend;
  char *resp;
  int nres = 0;
  int c1;
  int c2;

  while (1) {
    if (*pat == '*') { 
      star = ++pat; 			         /* Pattern after * */
      starend = dat; 			         /* Data after * match */
      if (res) {
        nres++;
        resp = res[nres]; 		     /* Result string */
        *resp = 0;   			         /* Initially null */
      }
    }
    else {
      c1 = tolower(*dat);
      if (*pat == '\\') {          /* check for \ in pattern */
        pat++;
        c2 = tolower(*pat); 
      }
      else {
        c2 = tolower(*pat);
        if (c2 == '?')               /* check for ? in pattern */
          c2 = c1;
      }
      if (c1 == c2) {              /* Characters match */
        if (*pat == 0) 		           /* Pattern matches */
	        return 1;
        pat++; 				               /* Try next position */
        dat++;
      }
      else { 
        if (*dat == 0) 		           /* Pattern fails - no more */
	        return 0;                  /* data */
        if (star == 0) 			         /* Pattern fails - no * to */
	        return 0; 			           /* adjust */
        pat = star; 			           /* Restart pattern after * */
        if (res) {
          *resp++ = *starend; 		     /* Copy character to result */
          *resp = 0; 			             /* null terminate */
        }
        dat = ++starend; 			       /* Rescan after copied char */
      }
    }
  }
}

void CreatePath(char *Path)
{
	char *o;
	char *p;
	char TPath[255];

	o = TPath;

	if (Path != OutDir) {
	  p = OutDir;
  	while (*p) {
	    *o++ = *p++;
		}
	  *o++ = '\\';
	}

	p = Path;
	while (*p) {
	  while (*p && (*p != '\\'))
			*o++ = *p++;
		*o = 0;
		_mkdir(TPath);
		if (*p)
			*o++ = *p++;
	}
}

int ReadAndDecrypt(int fpos, char *buff, int buffsize)
{
	int count;
	int tkey;
	int result;
	
	fseek(HPIFile, fpos, SEEK_SET);
	result = fread(buff, 1, buffsize, HPIFile);
  if (Key)
	  for (count = 0; count < buffsize; count++) {
  		tkey = (fpos + count) ^ Key;
      buff[count] = tkey ^ ~buff[count];
    }
	return result;
}

int LZ77Decompress(char *out, char *in, HPICHUNK *Chunk)
{
	int x;
	int work1;
	int work2;
	int work3;
	int inptr;
	int outptr;
	int count;
	int done;
	char DBuff[4096];
	int DPtr;

	done = FALSE;

  inptr = 0;
	outptr = 0;
	work1 = 1;
	work2 = 1;
	work3 = in[inptr++];
	
	while (!done) {
	  if ((work2 & work3) == 0) {
  		out[outptr++] = in[inptr];
		  DBuff[work1] = in[inptr];
		  work1 = (work1 + 1) & 0xFFF;
		  inptr++;
		}
	  else {
  		count = *((unsigned short *) (in+inptr));
			inptr += 2;
			DPtr = count >> 4;
			if (DPtr == 0) {
				return outptr;
			}
			else {
				count = (count & 0x0f) + 2;
				if (count >= 0) {
					for (x = 0; x < count; x++) {
						out[outptr++] = DBuff[DPtr];
						DBuff[work1] = DBuff[DPtr];
						DPtr = (DPtr + 1) & 0xFFF;
		        work1 = (work1 + 1) & 0xFFF;
					}

				}
			}
		}
		work2 *= 2;
		if (work2 & 0x0100) {
			work2 = 1;
			work3 = in[inptr++];
		}
	}

	return outptr;

}

int ZLibDecompress(char *out, char *in, HPICHUNK *Chunk)
{
  z_stream zs;
  int result;

  zs.next_in = in;
  zs.avail_in = Chunk->CompressedSize;
  zs.total_in = 0;

  zs.next_out = out;
  zs.avail_out = Chunk->DecompressedSize;
  zs.total_out = 0;

  zs.msg = NULL;
  zs.state = NULL;
  zs.zalloc = Z_NULL;
  zs.zfree = Z_NULL;
  zs.opaque = NULL;

  zs.data_type = Z_BINARY;
  zs.adler = 0;
  zs.reserved = 0;

  result = inflateInit(&zs);
  if (result != Z_OK) {
    printf("Error on inflateInit %d\nMessage: %s\n", result, zs.msg);
    return 0;
  }

  result = inflate(&zs, Z_FINISH);
  if (result != Z_STREAM_END) {
    printf("Error on inflate %d\nMessage: %s\n", result, zs.msg);
    return 0;
  }

  result = inflateEnd(&zs);
  if (result != Z_OK) {
    printf("Error on inflateEnd %d\nMessage: %s\n", result, zs.msg);
    return 0;
  }

	return zs.total_out;

}

int Decompress(char *out, char *in, HPICHUNK *Chunk)
{
	int Checksum;
	int x;

	Checksum = 0;
	for (x = 0; x < Chunk->CompressedSize; x++) {
	  Checksum += (unsigned char) in[x];
		if (Chunk->Encrypt)
		  in[x] = ((unsigned) in[x] - x) ^ x;
	}

  if (debug) {
    printf("Unknown1                  0x%02X\n", (unsigned) Chunk->Unknown1);
    printf("CompMethod:               %d\n", Chunk->CompMethod);
    printf("Encrypt:                  %d\n", Chunk->Encrypt);
    printf("CompressedSize:           %d\n", Chunk->CompressedSize);
    printf("DecompressedSize:         %d\n", Chunk->DecompressedSize);
    printf("SQSH Checksum:            0x%X\n", Chunk->Checksum);
		printf("Calculated SQSH checksum: 0x%X\n", Checksum); 
	}

	if (Chunk->Checksum != Checksum) {
		printf("*** SQSH checksum error! Calculated: 0x%X  Actual: 0x%X\n", Checksum, Chunk->Checksum);
		return 0;
	}

  switch (Chunk->CompMethod) {
    case 1 : return LZ77Decompress(out, in, Chunk);
    case 2 : return ZLibDecompress(out, in, Chunk);
    default : return 0;
  }
}


void ProcessFile(char *TName, int ofs, int len, int FileFlag)
{
  HPICHUNK *Chunk;
	long *DeSize;
	int DeCount;
	int x;
	char *DeBuff;
	char *WriteBuff;
	int WriteSize;
	int DeTotal;
	int CTotal;
	int DeLen;
	FILE *Sub;
	char Name[256];

	strcpy(Name, OutDir);
	strcat(Name, "\\");
	strcat(Name, TName);


	Sub = fopen(Name, "wb");
	if (!Sub) {
		printf("Error creating '%s'\n", Name);
		return;
	}
	printf("%s -> %s\n", TName, Name);

	if (debug)
		printf("Offset 0x%X\n", ofs);

	if (FileFlag) {
  	DeCount = len / 65536;
  	if (len % 65536)
		  DeCount++;
	  DeLen = DeCount * sizeof(int);
	  DeSize = malloc(DeLen);
	  DeTotal = 0;
	  CTotal = 0;

	  ReadAndDecrypt(ofs, (char *) DeSize, DeLen);
    ofs += DeLen;

	  if (debug)
  	  printf("\nChunks: %d\n", DeCount);

	  WriteBuff = malloc(65536);

	  for (x = 0; x < DeCount; x++) {
		  Chunk = malloc(DeSize[x]);

		  ReadAndDecrypt(ofs, (char *) Chunk, DeSize[x]);


		  if (debug) {
			  printf("Chunk %d: Compressed %d  Decompressed %d  SQSH Checksum 0x%X\n", x+1, Chunk->CompressedSize,
				       Chunk->DecompressedSize, Chunk->Checksum);
			  printf("   Unknown1: 0x%02X CompMethod: 0x%02X Encrypt: 0x%02X\n",
				       Chunk->Unknown1, Chunk->CompMethod, Chunk->Encrypt); 
			}

		  CTotal += Chunk->CompressedSize;
		  DeTotal += Chunk->DecompressedSize;

		  ofs += DeSize[x];

		  DeBuff = (void *) (Chunk+1);

      WriteSize = Decompress(WriteBuff, DeBuff, Chunk);

	    fwrite(WriteBuff, 1, WriteSize, Sub);
		  if (WriteSize != Chunk->DecompressedSize) {
  			printf("WriteSize (%d) != Chunk->DecompressedSize (%d)!\n", WriteSize, Chunk->DecompressedSize);
			}
		  free(Chunk);
		}
	  fclose(Sub);

	  if (debug)
		  printf("Total compressed: %d  Total decompressed: %d\n\n", CTotal, DeTotal);

	  free(WriteBuff);
	  free(DeSize);
	}
	else {
		WriteBuff = malloc(len);

	  ReadAndDecrypt(ofs, WriteBuff, len);
    fwrite(WriteBuff, 1, len, Sub);

		free(WriteBuff);
	}
}

void ProcessDirectory(char *StartPath, int offset)
{
	int *Entries;
	HPIENTRY *Entry;
	int count;
	char *Name;
	int *FileCount;
	int *FileLength;
	char *FileFlag;
	int *EntryOffset;
	char MyPath[256];
	char MyDir[256];
	int extract;
	int SCount;

	Entries = (int *) (Directory + offset);
	EntryOffset = Entries + 1;
	Entry = (HPIENTRY *) (Directory + *EntryOffset);

  for (count = 0; count < *Entries; count++) {
		Name = Directory + Entry->NameOffset;
		FileCount = (int *) (Directory + Entry->CountOffset);
		if (*StartPath) {
  	  strcpy(MyPath, StartPath);
	    strcat(MyPath, "\\");
		  strcat(MyPath, Name);
		}
		else
			strcpy(MyPath, Name);

		if (Entry->Flag == 1)	{
			if (debug)
    	  printf("Directory %s Files %d Flag %d\n", Name, *FileCount, Entry->Flag);
			if (!OutSpec[0]) {
				strcpy(MyDir, OutDir);
				strcat(MyDir, "\\");
				strcat(MyDir, MyPath);
			  _mkdir(MyDir);
			}
		  ProcessDirectory(MyPath, Entry->CountOffset);
		}
		else {
		  FileLength = FileCount + 1;
			FileFlag = (char *)	(FileLength + 1);
			extract = TRUE;
			if (OutSpec[0]) {
				SCount = 0;
				extract = FALSE;
				while (OutSpec[SCount]) {
					if (StarMatch(Name, OutSpec[SCount], NULL)) {
						extract = TRUE;
						break;
					}
					if (StarMatch(MyPath, OutSpec[SCount], NULL)) {
						extract = TRUE;
						break;
					}
					SCount++;
				}
				if (extract)
				  CreatePath(StartPath);
			}
			if (extract) {
			  if (debug)
    		  printf("File %s Data Offset %d Length %d Flag %d FileFlag %d\n", Name, *FileCount, *FileLength, Entry->Flag, *FileFlag);

  			ProcessFile(MyPath, *FileCount, *FileLength, *FileFlag);
			  TotalLength += *FileLength;
			}
		}
		Entry++;
	}
}

int CheckDirectory(char *DName)
{
	if (!DName[0]) {
		strcpy(DName, ".");
		return TRUE;
	}
	CreatePath(DName);
	return TRUE;
}

void PrintHelp(void)
{
	printf("Format:\n");
	printf("HPIDump filename.hpi [-o output_directory_name] [-d] [-h] [file1] [file2]\n\n");
	printf("filename.hpi            is the name of the file to extract\n\n");
	printf("output_directory_name   is the optional name of the\n");
	printf("                        directory to extract it to\n\n");
	printf("fileX                   One or more file specifications to extract.\n");
	printf("                        Wildcards allowed.\n");
	printf("Options:\n");
	printf("-h - Print this help message\n");
	printf("-d - Detailed output\n");
}

int ProcessCommandLine(int argc, char *argv[])
{
	int x;
	int SpecCount;
	int result = TRUE;
	char *i;
	char *o;
	char buff[1024];

	x = 1;
	SpecCount = 0;
	OutSpec = malloc(argc * sizeof(char *));
	while (x < argc) {
		if (argv[x][0] == '-') {
			switch (tolower(argv[x][1])) {
			  case '?' :
				case 'h' :
				  PrintHelp();
					return FALSE;
				case 'd' :
					debug = TRUE;
					printf("Detailed output enabled\n");
					break;
				case 'o' :
					x++;
					if (x == argc)
						break;
					strcpy(OutDir, argv[x]);
					break;
				default :
			    printf("Invalid command line option: %s\n", argv[x]);
			    result = FALSE;
					break;
			}
		}
		else if (!HPIName[0])
			strcpy(HPIName, argv[x]);
		else {
			i = argv[x];
			o = buff;
			while (*i) {
				if (*i == '\\')
					*o++ = '\\';
				*o++ = *i++;
			}
			*o = 0;
			OutSpec[SpecCount++] = _strdup(buff);
		}
		x++;
	}
	OutSpec[SpecCount] = NULL;
	if (!HPIName[0]) {
		PrintHelp();
		return FALSE;
	}

	return result;
}


void DumpV1(void)
{
  HPIHEADER1 h1;

  fread(&h1, sizeof(h1), 1, HPIFile);

	if (!CheckDirectory(OutDir)) {
		return;
	}

	if (OutDir[0])
		printf("Extracting %s to %s\n", HPIName, OutDir);
	else
		printf("Extracting %s\n", HPIName);

  if (h1.Key)
  	Key = ~((h1.Key * 4)	| (h1.Key >> 6));
  else
    Key = 0;

	CurPos = h1.Start;

	Directory = malloc(h1.DirectorySize);
	memset(Directory, 0, h1.Start);

	ReadAndDecrypt(h1.Start, Directory + h1.Start, h1.DirectorySize-h1.Start);

	ProcessDirectory("", h1.Start);

	printf("Total Length: %d\n", TotalLength);
}

int CheckCalc(long *cs, char *buff, long size)
{
  int count;
  unsigned int c;
  unsigned char *check = (unsigned char *) cs;

  for (count = 0; count < size; count++) {
    c = (unsigned char) buff[count];
    check[0] += c;
    check[1] ^= c;
    check[2] += (c ^ ((unsigned char) (count & 0x000000FF)));
    check[3] ^= (c + ((unsigned char) (count & 0x000000FF)));
  }
  return *cs;  
}

void ProcessFile2(char *OutPath, HPIENTRY2 *Entry, char *Dir, char *Name)
{
  char NewName[1024];
  char OutName[1024];
  struct tm *t;
  FILE *f;
  char *block;
  char *sqsh;
  long outlen;
  long outsize;
  HPICHUNK Chunk;
  int extract;
  int SCount;
  struct _utimbuf utb;
  long dcheck;

  strcpy(NewName, OutPath);
  strcat(NewName, Name+Entry->NamePtr);
  strcpy(OutName, OutDir);
  strcat(OutName, NewName);

	extract = TRUE;
	if (OutSpec[0]) {
		SCount = 0;
		extract = FALSE;
		while (OutSpec[SCount]) {
			if (StarMatch(Name+Entry->NamePtr, OutSpec[SCount], NULL)) {
				extract = TRUE;
				break;
			}
			if (StarMatch(NewName, OutSpec[SCount], NULL)) {
				extract = TRUE;
				break;
			}
			SCount++;
		}
		if (extract)
		  CreatePath(OutPath);
    else
      return;
	}
	printf("%s -> %s\n", NewName, OutName);

  t = localtime((time_t *) &Entry->Date);
  if (debug) {
    printf("Compressed size:    %8d\n", Entry->CompressedSize);
    printf("Decompressed size:  %8d\n", Entry->DecompressedSize);
    printf("Checksum:           0x%08X\n", Entry->Checksum);
    printf("Date:               %s", asctime(t));
  }

/*  printf("%02d-%02d-%04d %02d:%02d:%02d\n", 
    t->tm_mon+1, t->tm_mday, t->tm_year+1900,
    t->tm_hour, t->tm_min, t->tm_sec);
  printf("%9d  %9d  0x%08X\n", Entry->DecompressedSize, Entry->CompressedSize, Entry->Checksum);*/

  f = fopen(OutName, "wb");
  if (!f) {
    printf("Unable to create %s\n", OutName);
    return;
  }

  fseek(HPIFile, Entry->Start, SEEK_SET);

  dcheck = 0;
  if (Entry->CompressedSize) {
    outlen = 0;
    while (outlen < Entry->DecompressedSize) {
      fread(&Chunk, sizeof(Chunk), 1, HPIFile); 

      block = malloc(Chunk.DecompressedSize);
      sqsh = malloc(Chunk.CompressedSize);

      fread(sqsh, Chunk.CompressedSize, 1, HPIFile); 
      outsize = Decompress(block, sqsh, &Chunk);

      if (outsize != Chunk.DecompressedSize) {
        printf("** error decompressing!\n");
        break;
      }
      else {
        fwrite(block, Chunk.DecompressedSize, 1, f);
        outlen += Chunk.DecompressedSize;
      }
      CheckCalc(&dcheck, block, outsize);

      free(block);
      free(sqsh);
    }
  }
  else {
    block = malloc(Entry->DecompressedSize);
    fread(block, Entry->DecompressedSize, 1, HPIFile); 
    fwrite(block, Entry->DecompressedSize, 1, f);
    CheckCalc(&dcheck, block, Entry->DecompressedSize);
    free(block);
  }
  if (debug) {
    printf("HPI Checksum:             0x%08X\n", Entry->Checksum);
    printf("Calculated HPI Checksum:  0x%08X\n", dcheck);
  }
  if (Entry->Checksum != dcheck)
    printf("HPI checksum error!\nActual: 0x%08X  Calculated: 0x%08X\n", Entry->Checksum, dcheck);
  fclose(f);

  utb.actime = Entry->Date;
  utb.modtime = Entry->Date;

  _utime(OutName, &utb);
}

void ProcessDirectory2(char *OutPath, HPIDIR2 *hd, char *Dir, char *Name)
{
  HPIDIR2 *Sub;
  HPIENTRY2 *Entry;
  int count;
  char NewPath[1024];
  char DirPath[1024];

  strcpy(NewPath, OutPath);
  strcat(NewPath, Name+hd->NamePtr);

  strcpy(DirPath, OutDir);
  strcat(DirPath, "\\");
  strcat(DirPath, NewPath);

  if (!OutSpec[0])
    _mkdir(DirPath);

  strcat(NewPath, "\\");
  printf("%s\n", NewPath);

  if (hd->SubCount) {
    Sub = (HPIDIR2 *) (Dir + hd->FirstSubDir);
    for (count = 0; count < hd->SubCount; count++) {
      ProcessDirectory2(NewPath, Sub+count, Dir, Name);
    }
  }
  if (hd->FileCount) {
    Entry = (HPIENTRY2 *) (Dir + hd->FirstFile); 
    for (count = 0; count < hd->FileCount; count++) {
      ProcessFile2(NewPath, Entry+count, Dir, Name);
    }
  }


}

void DumpV2(void)
{
  HPIHEADER2 h2;
  HPICHUNK *Chunk;
  char *Dir;
  char *Name;
  char *block;
  int outsize;
  HPIDIR2 *root;

	if (!CheckDirectory(OutDir)) {
		return;
	}

	if (OutDir[0])
		printf("Extracting %s to %s\n", HPIName, OutDir);
	else
		printf("Extracting %s\n", HPIName);

  fread(&h2, sizeof(h2), 1, HPIFile);

  block = malloc(h2.DirSize);
  
  fseek(HPIFile, h2.DirBlock, SEEK_SET);
  fread(block, h2.DirSize, 1, HPIFile);

  Chunk = (void *) block;
  if (Chunk->Marker == HEX_SQSH) {
    Dir = malloc(Chunk->DecompressedSize);
    outsize = Decompress(Dir, block+sizeof(HPICHUNK), Chunk);
    if (outsize != Chunk->DecompressedSize) {
      printf("Error decompressing dir block.\n");
      return;
    }
    free(block);
  }
  else {
    Dir = block;
  }

  block = malloc(h2.NameSize);
  
  fseek(HPIFile, h2.NameBlock, SEEK_SET);
  fread(block, h2.NameSize, 1, HPIFile);
  
  Chunk = (void *) block;
  if (Chunk->Marker == HEX_SQSH) {
    Name = malloc(Chunk->DecompressedSize);
    outsize = Decompress(Name, block+sizeof(HPICHUNK), Chunk);
    if (outsize != Chunk->DecompressedSize) {
      printf("Error decompressing name block.\n");
      return;
    }
    free(block);
  }
  else {
    Name = block;
  }

  root = (void *) Dir;

  ProcessDirectory2("", root, Dir, Name);
  
  free(Name);
  free(Dir);

}

void main(int argc, char *argv[])
{
	printf("HPIDump 1.5.1 - HPI File Dumper\n");
	printf("Copyright 1999 The Center for Weird Studies\n");
	printf("by Joe D (joed@cws.org)\n\n");

	if (argc < 2) {
		PrintHelp();
		return;
	}

	if (!ProcessCommandLine(argc, argv))
		return;

  //gen_crc_table(0);

	HPIFile = fopen(HPIName, "rb");
	if (!HPIFile) {
		printf("File not found - %s\n", HPIName);
		return;
	}

	fread(&hv, sizeof(hv), 1, HPIFile);

	if (hv.HPIMarker != HEX_HAPI) {  /* 'HAPI' */
		fclose(HPIFile);
		printf("Not an HPI-format file.\n");
		return;
	}

  if (hv.Version == HEX_BANK) { /* 'BANK' */
		printf("TA Savegame file.\n");
		fclose(HPIFile);
		return;
	}

  if (hv.Version == 0x00010000) { /* version 1 */
    printf("%s is HPI version 1 (TA, TA:CC)\n", HPIName);
    DumpV1();
		fclose(HPIFile);
    
  }

  else if (hv.Version == 0x00020000) { /* version 2 */
    printf("%s is HPI version 2 (TAK)\n", HPIName);
    DumpV2();
		fclose(HPIFile);
  }
  else {
    printf("Invalid version: 0x%08X\n", hv.Version);
    fclose(HPIFile);
  }
  

}
