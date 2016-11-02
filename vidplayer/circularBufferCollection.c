#include <stdio.h>

#include "vidplayer.h"
#include "circularBufferCollection.h"

// typedef struct Buffer
// {
//   char *data;
//   unsigned int size;
//   unsigned int currentSize;

// } Buffer;

// typedef struct CircularBufferCollection 
// { 
//   Buffer **buffers;
//   unsigned int writeIndex;
//   unsigned int readIndex;
//   unsigned int count;

// } CircularBufferCollection;

CircularBufferCollection *circularBufferCollectionInit(unsigned int size,unsigned int count)
{
  DBG("[%s][%s] size/count: %d,%d\n",__FILE__,__FUNCTION__,size,count);                               

  CircularBufferCollection *bufferArray = (CircularBufferCollection *)malloc(sizeof(CircularBufferCollection));

  bufferArray->count = count;
  bufferArray->currentCount = 0;
  bufferArray->writeIndex = 0;
  bufferArray->readIndex = 0;

  bufferArray->buffers = (Buffer **)malloc(sizeof(Buffer*)*count);

  int i;

  DBG("[%s][%s] Init array\n",__FILE__,__FUNCTION__);                               

  for(i=0;i<count;i++)
  {
    bufferArray->buffers[i] = (Buffer *)malloc(sizeof(Buffer));

    bufferArray->buffers[i]->size = size;
    bufferArray->buffers[i]->currentSize = 0;
    bufferArray->buffers[i]->data = (char *)malloc(size*sizeof(char));
  }

  return bufferArray;
}

void circularBufferCollectionDestroy(CircularBufferCollection *bufferArray)
{
  int i;
  for(i=0;i<bufferArray->count;i++)
  {
    free(bufferArray->buffers[i]->data);
    free(bufferArray->buffers[i]);
  }

  free(bufferArray->buffers);
  free(bufferArray);
}

unsigned int isBufferCircularCollectionFull(CircularBufferCollection *bufferArray)
{
  DBG("[%s][%s] %d/%d\n",__FILE__,__FUNCTION__,bufferArray->currentCount,bufferArray->count);                               

  return bufferArray->currentCount == bufferArray->count;
}

unsigned int isBufferCircularCollectionEmpty(CircularBufferCollection *bufferArray)
{
  return bufferArray->currentCount == 0;
}
