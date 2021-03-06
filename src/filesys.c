/* filesys.c
 * 
 * provides interface to virtual disk
 * 
 */
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "filesys.h"


diskblock_t  virtualDisk [MAXBLOCKS] ;           // define our in-memory virtual, with MAXBLOCKS blocks
fatentry_t   FAT         [MAXBLOCKS] ;           // define a file allocation table with MAXBLOCKS 16-bit entries
fatentry_t   rootDirIndex            = 0 ;
fatentry_t   currentDirIndex         = 0 ;


/* writedisk : writes virtual disk out to physical disk
 * 
 * in: file name of stored virtual disk
 */

void writedisk ( const char * filename )
{
   printf ( "writedisk> virtualdisk[0] = %s\n", virtualDisk[0].data ) ;
   FILE * dest = fopen( filename, "w" ) ;
   if ( fwrite ( virtualDisk, sizeof(virtualDisk), 1, dest ) < 0 )
      fprintf ( stderr, "write virtual disk to disk failed\n" ) ;
   //write( dest, virtualDisk, sizeof(virtualDisk) ) ;
   fclose(dest) ;
   
}

void readdisk ( const char * filename )
{
   FILE * dest = fopen( filename, "r" ) ;
   if ( fread ( virtualDisk, sizeof(virtualDisk), 1, dest ) < 0 )
      fprintf ( stderr, "read virtual disk to disk failed\n" ) ;
   //write( dest, virtualDisk, sizeof(virtualDisk) ) ;
      fclose(dest) ;
}


/* the basic interface to the virtual disk
 * this moves memory around
 */

void writeblock ( diskblock_t * block, int block_address )
{
   //printf ( "writeblock> block %d = %s\n", block_address, block->data ) ;
   memmove ( virtualDisk[block_address].data, block->data, BLOCKSIZE ) ;
   //printf ( "writeblock> virtualdisk[%d] = %s / %d\n", block_address, virtualDisk[block_address].data, (int)virtualDisk[block_address].data ) ;
}



/* read and write FAT
 * 
 * please note: a FAT entry is a short, this is a 16-bit word, or 2 bytes
 *              our blocksize for the virtual disk is 1024, therefore
 *              we can store 512 FAT entries in one block
 * 
 *              how many disk blocks do we need to store the complete FAT:
 *              - our virtual disk has MAXBLOCKS blocks, which is currently 1024
 *                each block is 1024 bytes long
 *              - our FAT has MAXBLOCKS entries, which is currently 1024
 *                each FAT entry is a fatentry_t, which is currently 2 bytes
 *              - we need (MAXBLOCKS /(BLOCKSIZE / sizeof(fatentry_t))) blocks to store the
 *                FAT
 *              - each block can hold (BLOCKSIZE / sizeof(fatentry_t)) fat entries
 */

// Stores the FAT array in the virtual disk as diskblock_t
void copyFAT()
{
   	diskblock_t block ;
	int i;
	int j;
	for (i = 0; i < FATBLOCKSNEEDED; i++)
	{
		for (j = 0; j < FATENTRYCOUNT; j++)
		{
			block.fat[j] = FAT[((i*FATENTRYCOUNT)+j)];
		}
		writeblock(&block, i + 1);
	}
}

// Creates a new block in the virtual disk ready for a directory.
int newDirBlock()
{
	
   	diskblock_t block ;
	int i;
	int workingDirIndex = currentDirIndex;
	while (FAT[workingDirIndex] != ENDOFCHAIN) workingDirIndex = FAT[workingDirIndex];
	if (virtualDisk[workingDirIndex].dir.nextEntry == DIRENTRYCOUNT)
	{
		int blockno = findFreeBlock();
		for (i = 0; i < BLOCKSIZE; i++)
		{
			block.data[i] = '\0';
		}
		FAT[workingDirIndex] = blockno;
		FAT[blockno] = ENDOFCHAIN;
		copyFAT();
		block.dir.isdir = 0;	//hmm?
		block.dir.nextEntry = 0;
		writeblock(&block, blockno);
		return blockno;
	}
	return workingDirIndex;
}

// Formats the virtual disk and creates a new FAT and rootdir.
void format ( )
{
   	diskblock_t block ;
	int i;
	for (i = 0; i < BLOCKSIZE; i++)
	{
		block.data[i] = '\0';
	}
	strcpy(block.data, "CS3008 Operating Systems Assessment 2012");
	writeblock(&block, 0);
	for (i = 0; i < MAXBLOCKS; i++)
	{
		FAT[i] = UNUSED;
	}
	FAT[0] = ENDOFCHAIN;	//FAT entry for reserved block
	for (i = 1; i <= FATBLOCKSNEEDED; i++)
	{
		if (i < FATBLOCKSNEEDED)
		{
			FAT[i] = i+1;
		}
		else
		{
			FAT[i] = ENDOFCHAIN;
		}
	}
	FAT[FATBLOCKSNEEDED + 1] = ENDOFCHAIN;	//FAT entry for root
	copyFAT();
	
	
	for (i = 0; i < BLOCKSIZE; i++)
	{
		block.data[i] = '\0';
	}
	block.dir.isdir = 1;
	block.dir.nextEntry = 0;
	writeblock(&block, FATBLOCKSNEEDED + 1);

	rootDirIndex = FATBLOCKSNEEDED + 1;
	currentDirIndex = rootDirIndex;
	
}

// Returns the number of the first free block in the virtual disk.
// If disk full, returns -1.
int findFreeBlock()
{
	int i;
	for (i = 0; i < MAXBLOCKS; i++)
	{
		if (FAT[i] == UNUSED) return i;
	}
	fprintf(stderr, " WARNING: Disk is full.\n");
	return -1;
}


//Creates a new empty file in the current dir.
void filecreate(const char * name)
{
	int workingDirIndex = currentDirIndex;
	if (virtualDisk[currentDirIndex].dir.nextEntry == DIRENTRYCOUNT)
	{
		workingDirIndex = newDirBlock();
	}
	direntry_t * entrypointer = &(virtualDisk[workingDirIndex].dir.entrylist[(virtualDisk[workingDirIndex].dir.nextEntry)]);
	virtualDisk[workingDirIndex].dir.nextEntry ++;
	(*entrypointer).isdir = 0;
	memmove ( (*entrypointer).name, name, MAXNAME ) ;
	int blockno = findFreeBlock();
	(*entrypointer).firstblock = blockno;
	(*entrypointer).unused = 0;
	FAT[blockno] = ENDOFCHAIN;
	(*entrypointer).filelength = 0;
}

//Creates and returns a MyFILE stream for file input/output.
MyFILE * fileopen ( const char * name, const char * mode )
{
	int i;
	int found = 0;
	int blockno;
	int entryno;
	int workingDirIndex = currentDirIndex;
	int length;
	for (i = 0; i < DIRENTRYCOUNT; i++)
	{
		if (
				(strcmp(name, (virtualDisk[workingDirIndex].dir.entrylist[i].name)) == 0)
				&&	(virtualDisk[workingDirIndex].dir.entrylist[i].isdir == 0)  
				&& 	(virtualDisk[workingDirIndex].dir.entrylist[i].unused == 0)  
		   ) 
			{
				found = 1;
				blockno = virtualDisk[workingDirIndex].dir.entrylist[i].firstblock;
				entryno = i;
				break;
			}
		if ( (i == DIRENTRYCOUNT - 1) && (FAT[workingDirIndex] != ENDOFCHAIN) )
		{
			workingDirIndex = FAT[workingDirIndex];
			i = -1;
		}
	}
	if (found == 0)
	{
		return NULL;
	}
	MyFILE * stream = malloc(sizeof(MyFILE));
	(*stream).pos  = 0;
	(*stream).globalpos  = 0;
	(*stream).hasnext = 1;
	if (*mode == 'w') (*stream).writemode = 1; else (*stream).writemode = 0;
	(*stream).blockno = blockno;
	(*stream).dirIndex = workingDirIndex;
	(*stream).entryNo = entryno;
	(*stream).filelength = virtualDisk[workingDirIndex].dir.entrylist[entryno].filelength;
	memmove ( (*stream).buffer.data, virtualDisk[blockno].data, BLOCKSIZE ) ;
	return stream;
}


//Calls fileopen(...) and filecreate(...) if file has to be created.
MyFILE * openOrCreateFile ( const char * name, const char * mode)
{
	MyFILE * stream = fileopen(name, mode);
	if ( (stream == NULL) && ((mode)[0] != 'r') )
	{
		filecreate(name);
		return fileopen(name, mode);
	}
	return stream;
}
		
//Closes and frees memory cells taken by the stream and writes out any left data to the virtual disk.
void myfclose(MyFILE * stream)
{
	if ((*stream).writemode == 1) writeblock(&((*stream).buffer), (*stream).blockno);
	virtualDisk[(*stream).dirIndex].dir.entrylist[(*stream).entryNo].filelength = (*stream).filelength;
	time(&(virtualDisk[(*stream).dirIndex].dir.entrylist[(*stream).entryNo].modtime));
	copyFAT();
	free(stream);
}

//Returns the next byte in stream. If end of file reached, returns -1.
int myfgetc(MyFILE * stream)
{
	if ((*stream).hasnext == 0) return EOF;
	if ((*stream).pos == BLOCKSIZE)
	{
		int nextblock = FAT[(*stream).blockno];
		(*stream).pos = 0;
		(*stream).blockno = nextblock;
		memmove ( (*stream).buffer.data, virtualDisk[nextblock].data, BLOCKSIZE ) ;
	}
	(*stream).pos++;
	(*stream).globalpos++;
	if ((*stream).globalpos == (*stream).filelength) (*stream).hasnext = 0;
	return (*stream).buffer.data[(*stream).pos-1];
	
}

//Returns 1 if end of file not yet reached.
int myhasnext(MyFILE * stream)
{
	return (*stream).hasnext;
}

//Writes a byte in the stream and updates file length. If current block is filled, writes out to disk and creates a new block. Also modifies FAT according to changes.
void myfputc(int b, MyFILE * stream)
{
	if ((*stream).writemode == 0)
	{
		fprintf(stderr, "Can't write! File opened in read-only!\n");
		return;
	}

	if ((*stream).pos == BLOCKSIZE)
	{
		int freeblock = findFreeBlock();	
		writeblock( &((*stream).buffer), (*stream).blockno ) ;
		int i;
		for (i = 0; i < BLOCKSIZE; i++)
		{
			(*stream).buffer.data[i] = '\0';
		}
		FAT[(*stream).blockno] = freeblock;
		FAT[freeblock] = ENDOFCHAIN;
		copyFAT();
		(*stream).blockno = freeblock;
		(*stream).pos = 0;
		
	}
	(*stream).buffer.data[(*stream).pos] = b;
	(*stream).pos++;
	(*stream).globalpos++;
	(*stream).filelength++;
}


//Creates a new directory in the current dir.
void makedir(char * name)
{
	int workingDirIndex = currentDirIndex;
	if (virtualDisk[currentDirIndex].dir.nextEntry == DIRENTRYCOUNT)
	{
		workingDirIndex = newDirBlock();
	}
	direntry_t * entrypointer = &(virtualDisk[workingDirIndex].dir.entrylist[(virtualDisk[workingDirIndex].dir.nextEntry)]);
	virtualDisk[workingDirIndex].dir.nextEntry ++;
	(*entrypointer).isdir = 1;
	memmove ( (*entrypointer).name, name, MAXNAME ) ;
	int blockno = findFreeBlock();
	(*entrypointer).firstblock = blockno;
	FAT[blockno] = ENDOFCHAIN;
	copyFAT();
	virtualDisk[blockno].dir.isdir = 1;
	virtualDisk[blockno].dir.nextEntry = 2;
	//Create self link:
	memmove ( virtualDisk[blockno].dir.entrylist[0].name, ".", MAXNAME ) ;
	virtualDisk[blockno].dir.entrylist[0].firstblock = blockno;
	virtualDisk[blockno].dir.entrylist[0].isdir = 1;

	//Create parent link:
	memmove ( virtualDisk[blockno].dir.entrylist[1].name, "..", MAXNAME ) ;
	virtualDisk[blockno].dir.entrylist[1].firstblock = currentDirIndex;
	virtualDisk[blockno].dir.entrylist[1].isdir = 1;
	
}

//Changes the current dir if there exists such in the current dir entry.
int changedir(char * name)
{
	int i;
	int found = 0;
	int workingDirIndex = currentDirIndex;
	int blockno;
	for (i = 0; i < DIRENTRYCOUNT; i++)
	{
		if ((strcmp(name, (virtualDisk[workingDirIndex].dir.entrylist[i].name)) == 0) &&    (virtualDisk[workingDirIndex].dir.entrylist[i].isdir == 1)    ) 
			{
				blockno = virtualDisk[workingDirIndex].dir.entrylist[i].firstblock;
				found = 1;
				break;
			}
		if ( (i == DIRENTRYCOUNT - 1) && (FAT[workingDirIndex] != ENDOFCHAIN) )
		{
			workingDirIndex = FAT[workingDirIndex];
			i = -1;
		}
	}
	if (found == 1)
	{
		currentDirIndex = blockno;
		return 1;
	}
	else
	{
		return 0;
	}
}

//Returns an array of strings containing the contents of the current dir.
char ** listdir( )
{
	int i;
	int j = 0;
	int workingDirIndex = currentDirIndex;
	char **content;
	content = (char**) malloc(1000*MAXNAME*sizeof(char));  
	for (i = 2; i < DIRENTRYCOUNT; i++)
	{
		content[j*DIRENTRYCOUNT+i-2] = (char*) malloc(MAXNAME*sizeof(char));
		if (virtualDisk[workingDirIndex].dir.entrylist[i].unused == 0)
		{
			strcpy(content[j*DIRENTRYCOUNT+i-2], virtualDisk[workingDirIndex].dir.entrylist[i].name);
		
			if ( (i == DIRENTRYCOUNT - 1) && (FAT[workingDirIndex] != ENDOFCHAIN) )
			{
				workingDirIndex = FAT[workingDirIndex];
				i = -1;
				j++;
			}
			if ( (i == DIRENTRYCOUNT - 1) && (FAT[workingDirIndex] == ENDOFCHAIN) )
			{
				content[j*DIRENTRYCOUNT+i-1] = NULL; break;
			}
		}
	}
	return content;
} 

//Returns a single string containing ALL folders and files in disk, listed recursively.
char * listAllRecursively( int index, int level)
{
	int i;
	int size = 100000;	//Because of recursion
	int workingDirIndex = index;
	char *content = malloc(size);
	strcpy(content, "");
	int startingpoint = 2;
	if (index == rootDirIndex) startingpoint = 0;
	for (i = startingpoint; i < virtualDisk[workingDirIndex].dir.nextEntry; i++)
	{
		if (virtualDisk[workingDirIndex].dir.entrylist[i].unused == 0)
		{
			int j;
			for (j = 0; j < level; j++)strcat(content, "     ");
			int folder = 0;
			strcat(content, virtualDisk[workingDirIndex].dir.entrylist[i].name);
			if (virtualDisk[workingDirIndex].dir.entrylist[i].isdir == 1)
			{
				strcat(content, " <DIR>");
				folder = 1;
			}
			strcat(content, "\n");
		
			if (folder == 1) strcat(content, listAllRecursively(virtualDisk[workingDirIndex].dir.entrylist[i].firstblock, level+1));
			
		}
		if ( (i == DIRENTRYCOUNT - 1) && (FAT[workingDirIndex] != ENDOFCHAIN) )
		{
			workingDirIndex = FAT[workingDirIndex];
			i = -1;
		}
	}
	return content;
}

//Removes an entry from virtual disk and FAT given its name and if it's a directory.
int removeEntry(char * name, int isdir)
{
	int i;
	int workingDirIndex = currentDirIndex;
	for (i = 0; i < DIRENTRYCOUNT; i++)
	{
		if ((strcmp(name, (virtualDisk[workingDirIndex].dir.entrylist[i].name)) == 0) 
		&&    (virtualDisk[workingDirIndex].dir.entrylist[i].isdir == isdir)  
		&& (virtualDisk[workingDirIndex].dir.entrylist[i].unused == 0)    ) 
		{
			strcpy(virtualDisk[workingDirIndex].dir.entrylist[i].name, "*DELETED*");
			virtualDisk[workingDirIndex].dir.entrylist[i].unused = 1;

			int workingFileIndex = virtualDisk[workingDirIndex].dir.entrylist[i].firstblock;
			virtualDisk[workingDirIndex].dir.entrylist[i].firstblock = UNUSED;
			do
			{
				for (i = 0; i < BLOCKSIZE; i++)
				{
					virtualDisk[workingFileIndex].data[i] = '\0';
				}
				int temp = workingFileIndex;
				workingFileIndex = FAT[workingFileIndex];
				FAT[temp] = UNUSED;
				
			}
			while (workingFileIndex != ENDOFCHAIN);
			
			copyFAT();
			return 1;
			
		}
		if ( (i == DIRENTRYCOUNT - 1) && (FAT[workingDirIndex] != ENDOFCHAIN) )
		{
			workingDirIndex = FAT[workingDirIndex];
			i = -1;
		}
	}
	return 0;
}

//Removes a directory and its contents, recursively.
int removeDirAndContents(char * name)
{ 	
	if (changedir(name) == 0) return 0;
	int i; 
	int workingDirIndex = currentDirIndex;
	for (i = 2; i < virtualDisk[workingDirIndex].dir.nextEntry; i++)
	{
		if (virtualDisk[workingDirIndex].dir.entrylist[i].isdir == 1)
		{
			if (virtualDisk[workingDirIndex].dir.entrylist[i].unused == 0) removeDirAndContents(virtualDisk[workingDirIndex].dir.entrylist[i].name);
		}
		else
		{
			removeEntry(virtualDisk[workingDirIndex].dir.entrylist[i].name, 0);
		}
		if ( (i == DIRENTRYCOUNT - 1) && (FAT[workingDirIndex] != ENDOFCHAIN) )
		{
			workingDirIndex = FAT[workingDirIndex];
			i = -1;
		}
	}
	changedir("..");
	removeEntry(name, 1);
	return 1;
}

//Function handling relative and absolute path strings. 
int pathaction(const char * path, int action)
{
	int currentDirIndexBackup = currentDirIndex;
	if (path[0] == '/') currentDirIndex = rootDirIndex;

	char copy[100];
	strcpy(copy, path);
	char * p = copy;
	char * current;
	char * next;
	next = strtok_r(p, "/", &p);
	while (next != NULL)
	{
		current = next;
		next = strtok_r(p, "/", &p);

		switch (action)		//Different cases
		{
			case 1:	//chdir
				if (changedir(current) == 0) {currentDirIndex = rootDirIndex; return 0;}
				break;
			case 2: //mkdir
				if (changedir(current) == 0) {makedir(current);changedir(current);}
				if (next == NULL) currentDirIndex = currentDirIndexBackup;
				break;
			case 3: //rmdir
				if (next != NULL) {if (changedir(current) == 0) {currentDirIndex = rootDirIndex; return 0;}}
				else return removeDirAndContents(current);
				break;
			case 4: //rmfile
				if (next != NULL) {if (changedir(current) == 0) {currentDirIndex = rootDirIndex; return 0;}}
				else return removeEntry(current, 0);
				break;
			case 5:	//mkdir and chdir for fileopen
				if (next != NULL) if (changedir(current) == 0) {makedir(current);changedir(current);}
				break;
			default:
				break;
		}
	}
	return 1;
}


//Look at API
MyFILE * myfopen ( const char * path, const char * mode)
{
	int currentDirIndexBackup = currentDirIndex;
	char copy[100];
	strcpy(copy, path);
	char * p = copy;
	char * current;
	char * next;
	next = strtok_r(p, "/", &p);
	while (next != NULL) {current = next;next = strtok_r(p, "/", &p);}
	pathaction(path, 5);
	char * filename = current;
	MyFILE * stream = openOrCreateFile(filename, mode);
	currentDirIndex = currentDirIndexBackup;
	return stream;
}

//Lists ALL contents of disk recursively
char * mylistdisk()
{
	char *content = malloc(100000);
	strcpy(content, "     > LISTING ALL FILES AND FOLDERS:\n");
	strcat(content, listAllRecursively(rootDirIndex, 0));
	strcat(content, "     > END OF LIST <\n");
	return content;
}

//Look at API
char ** mylistdir(const char * path)
{
	int backupIndex = currentDirIndex;
	if (pathaction(path, 1) == 0) { fprintf(stderr, "Path doesn't exist!\n"); currentDirIndex = backupIndex; return NULL; }
	return listdir();
}

//Look at API
void mychdir( const char * path )
{
	int backupIndex = currentDirIndex;
	if (pathaction(path, 1) == 0) { fprintf(stderr, "Path doesn't exist!\n"); currentDirIndex = backupIndex; }
}

//Look at API
void mymkdir( const char * path )
{
	pathaction(path, 2);	
}

//Look at API
void myrmdir( const char * path )
{
	if (pathaction(path, 3) == 0) fprintf(stderr, "Path or folder doesn't exist!\n");
}

//Look at API
void myremove ( const char * path )
{
	if (pathaction(path, 4) == 0) fprintf(stderr, "Path or file doesn't exist!\n");
}

//Look at API
void myreadstringfile(char * name)
{	
	MyFILE * reader = myfopen(name, "r");
	if (reader == NULL) {fprintf(stderr, "No file named '%s'\n", name); return;}
	int i;
	printf("     > File content of '%s':\n", name);
	while (myhasnext(reader) == 1)
	{
		printf("%c", myfgetc(reader));
	}
	printf("\n     > END OF FILE <\n");
	myfclose(reader);
}

//Creates a file and writes a string in it.
void mywritestringfile(char * name,char*data)
{
	MyFILE * writer = myfopen(name, "w");
	int i;
	for (i = 0; i < strlen(data); i++)
	{
		myfputc(data[i], writer);
	}
	myfclose(writer);
}

//Copies a file from current dir to real harddisk.
void mycopytorealdisk ( char * filename, char * destname )
{
	FILE * ofp = fopen( destname, "w" ) ;
	MyFILE * stream = myfopen(filename, "r");
	if (stream == NULL) {fprintf(stderr, "No file named '%s'\n", filename); return;}
	int i;
	for (i = 0; i < (*stream).filelength; i++)
	{
		int buffer = myfgetc(stream);
		fwrite(&buffer, 1, 1, ofp);
	}
	
	fclose(ofp) ;
	myfclose(stream);
   
}

//Copies a file from real disk to cirrent dir of virtual disk
void mycopyfromrealdisk ( char * filename )
{
	FILE * ifp = fopen( filename, "r" ) ; if (ifp == NULL) {fprintf(stderr, "Can't open input file in list!\n"); return;}
	fseek(ifp, 0, SEEK_END);
	int size = ftell(ifp);
	fseek(ifp, 0, SEEK_SET);

	MyFILE * stream = myfopen(filename, "w");
	int i;
	int buffer;
	for (i = 0; i < size; i++)
	{
		fread(&buffer, 1, 1, ifp);
		myfputc(buffer, stream);
	}
	
	fclose(ifp) ;
	myfclose(stream);
}

//Copies a file within the virtual disk
void mycopyfile( char * pathsrc, char * pathdest)
{
	MyFILE * reader = myfopen(pathsrc, "r");
	if (reader == NULL) {fprintf(stderr, "No file named '%s'\n", pathsrc); return;}
	MyFILE * writer = myfopen(pathdest, "w");
	int i;
	while (myhasnext(reader) == 1)
	{
		myfputc(myfgetc(reader), writer);
	}
	myfclose(reader);
	myfclose(writer);
}

//Moves a file within the virtual disk
void mymovefile( char * pathsrc, char * pathdest)
{
	MyFILE * reader = myfopen(pathsrc, "r");
	if (reader == NULL) {fprintf(stderr, "No file named '%s'\n", pathsrc); return;}
	MyFILE * writer = myfopen(pathdest, "w");
	int i;
	while (myhasnext(reader) == 1)
	{
		myfputc(myfgetc(reader), writer);
	}
	myfclose(reader);
	myfclose(writer);
	myremove(pathsrc);
}

/* use this for testing
 */

void printBlock ( int blockIndex )
{
   printf ( "virtualdisk[%d] = %s\n", blockIndex, virtualDisk[blockIndex].data ) ;
}

