#ifndef CIRCULARBUFFERCOLLECTION_H
#define CIRCULARBUFFERCOLLECTION_H

typedef struct Buffer
{
	char *data;
	unsigned int size;
	unsigned int currentSize;

} Buffer;

typedef struct CircularBufferCollection 
{ 
  Buffer **buffers;
  unsigned int writeIndex;
  unsigned int readIndex;
  unsigned int currentCount;
  unsigned int count;

} CircularBufferCollection;

CircularBufferCollection *circularBufferCollectionInit(unsigned int size,unsigned int count);
void circularBufferCollectionDestroy(CircularBufferCollection*);

unsigned int isBufferCircularCollectionFull(CircularBufferCollection*);
unsigned int isBufferCircularCollectionEmpty(CircularBufferCollection*);

#endif