#include "tioga.h"
/**
 * set communicator
 * and initialize a few variables
 */

void tioga::setCommunicator(MPI_Comm communicator, int id_proc, int nprocs)
{
  scomm=communicator;
  myid=id_proc;
  numprocs=nprocs;
  sendCount=(int *) malloc(sizeof(int)*numprocs);
  recvCount=(int *) malloc(sizeof(int)*numprocs);
  //
  // only one mesh block per process for now
  // this can be changed at a later date
  // but will be a fairly invasive change
  //
  nblocks=1;
  mb=new MeshBlock[1];
  //
  // instantiate the parallel communication class
  //
  pc=new parallelComm[1];
  pc->myid=myid;
  pc->scomm=scomm;
  pc->numprocs=numprocs;  
}
/**
 * register grid data for each mesh block
 */
void tioga::registerGridData(int btag,int nnodes,double *xyz,int *ibl, int nwbc,int nobc,
			     int *wbcnode,int *obcnode,int ntypes, int *nv, int *nc, int **vconn)
{
  mb->setData(btag,nnodes,xyz,ibl,nwbc,nobc,wbcnode,obcnode,ntypes,nv,nc,vconn);
  mb->myid=myid;
  mytag=btag;
}

void tioga::profile(void)
{
  mb->preprocess();
  //mb->writeGridFile(myid);
  //mb->writeOBB(myid);
  //if (myid==4) mb->writeOutput(myid);
  //if (myid==4) mb->writeOBB(myid);
}

void tioga::performConnectivity(void)
{
  getHoleMap();
  exchangeBoxes();
  exchangeSearchData();
  mb->search();
  exchangeDonors();
  if (ihigh) {
  mb->getCellIblanks();
  mb->writeCellFile(myid);
  }
  //mb->writeOutput(myid);
  //tracei(myid);
}

void tioga::performConnectivityHighOrder(void)
{
  mb->getInternalNodes();
  exchangePointSearchData();
  mb->ihigh=1;
  mb->search();
  mb->processPointDonors();
  iorphanPrint=1;
}  

void tioga::dataUpdate(int nvar,double *q,int interptype)
{
  int i,j,k,m;
  int nints;
  int nreals;
  int *integerRecords;
  double *realRecords;
  int nsend,nrecv;
  int *sndMap,*rcvMap;
  PACKET *sndPack,*rcvPack;
  int *icount,*dcount;
  //
  // initialize send and recv packets
  //
  icount=dcount=NULL;
  integerRecords=NULL;
  realRecords=NULL;
  //
  pc->getMap(&nsend,&nrecv,&sndMap,&rcvMap);
  if (nsend==0) return;
  sndPack=(PACKET *)malloc(sizeof(PACKET)*nsend);
  rcvPack=(PACKET *)malloc(sizeof(PACKET)*nrecv);
  icount=(int *)malloc(sizeof(int)*nsend);
  dcount=(int *)malloc(sizeof(int)*nrecv);
  //
  pc->initPackets(sndPack,rcvPack);  
  //
  // get the interpolated solution now
  //
  integerRecords=NULL;
  realRecords=NULL;
  mb->getInterpolatedSolution(&nints,&nreals,&integerRecords,&realRecords,q,nvar,interptype);
  //
  // populate the packets
  //
  for(i=0;i<nints;i++)
    {
      k=integerRecords[2*i];
      sndPack[k].nints++;
      sndPack[k].nreals+=nvar;
    }

  for(k=0;k<nsend;k++)
    {
     sndPack[k].intData=(int *)malloc(sizeof(int)*sndPack[k].nints);
     sndPack[k].realData=(double *)malloc(sizeof(double)*sndPack[k].nreals);
     icount[k]=dcount[k]=0;
    }  

  m=0;
  for(i=0;i<nints;i++)
    {
      k=integerRecords[2*i];
      sndPack[k].intData[icount[k]++]=integerRecords[2*i+1];
      for(j=0;j<nvar;j++)
	sndPack[k].realData[dcount[k]++]=realRecords[m++];
    }
  //
  // communicate the data across
  //
  pc->sendRecvPackets(sndPack,rcvPack);
  //
  // decode the packets and update the data
  //
  for(k=0;k<nrecv;k++)
    {
      m=0;
      for(i=0;i<rcvPack[k].nints;i++)
	{
	  mb->updateSolnData(rcvPack[k].intData[i],&rcvPack[k].realData[m],q,nvar,interptype);
	  m+=nvar;
	}
    }
  //
  // release all memory
  //
  pc->clearPackets(sndPack,rcvPack);
  free(sndPack);
  free(rcvPack);
  if (integerRecords) free(integerRecords);
  if (realRecords) free(realRecords);
  if (icount) free(icount);
  if (dcount) free(dcount);
}

void tioga::writeData(int nvar,double *q,int interptype)
{
  //mb->writeGridFile(myid);
  mb->writeFlowFile(myid,q,nvar,interptype);
}

void tioga::getDonorCount(int *dcount,int *fcount)
{
  mb->getDonorCount(dcount,fcount);
}

void tioga::getDonorInfo(int *receptors,int *indices,double *frac,int *dcount)
{
  int nsend,nrecv;
  int *sndMap,*rcvMap;
  int i;
  mb->getDonorInfo(receptors,indices,frac);
  pc->getMap(&nsend,&nrecv,&sndMap,&rcvMap); 
  //
  // change to actual processor id here
  //
  for(i=0;i<3*(*dcount);i+=3)
    receptors[i]=sndMap[receptors[i]];
      
}

tioga::~tioga()
{      
  int i;
  if (mb) delete[] mb;
  if (holeMap) 
    {
      for(i=0;i<nmesh;i++)
	if (holeMap[i].existWall) free(holeMap[i].sam);
      delete [] holeMap;
    }
  if (pc) delete[] pc;
  if (sendCount) free(sendCount);
  if (recvCount) free(recvCount);
  if (obblist) free(obblist);
  if (myid==0) printf("#tioga :successfully cleared all the memory accessed\n");
};

void tioga::dataUpdate_highorder(int nvar,double *q,int interptype)
{
  int i,j,k,m;
  int nints;
  int nreals;
  int *integerRecords;
  double *realRecords;
  int nsend,nrecv;
  int *sndMap,*rcvMap;
  PACKET *sndPack,*rcvPack;
  int *icount,*dcount;
  double *qtmp;
  int norphanPoint;
  int *itmp;
  FILE *fp;
  char ofname[10];
  //
  // initialize send and recv packets
  //
  fp=NULL;
  icount=dcount=NULL;
  integerRecords=NULL;
  realRecords=NULL;
  //
  pc->getMap(&nsend,&nrecv,&sndMap,&rcvMap);
  if (nsend==0) return;
  sndPack=(PACKET *)malloc(sizeof(PACKET)*nsend);
  rcvPack=(PACKET *)malloc(sizeof(PACKET)*nrecv);
  icount=(int *)malloc(sizeof(int)*nsend);
  dcount=(int *)malloc(sizeof(int)*nrecv);
  //
  pc->initPackets(sndPack,rcvPack);  
  //
  // get the interpolated solution now
  //
  integerRecords=NULL;
  realRecords=NULL;
  mb->getInterpolatedSolutionAtPoints(&nints,&nreals,&integerRecords,
				      &realRecords,q,nvar,interptype);
  //
  // populate the packets
  //
  for(i=0;i<nints;i++)
    {
      k=integerRecords[2*i];
      sndPack[k].nints++;
      sndPack[k].nreals+=nvar;
    }

  for(k=0;k<nsend;k++)
    {
     sndPack[k].intData=(int *)malloc(sizeof(int)*sndPack[k].nints);
     sndPack[k].realData=(double *)malloc(sizeof(double)*sndPack[k].nreals);
     icount[k]=dcount[k]=0;
    }  

  m=0;
  for(i=0;i<nints;i++)
    {
      k=integerRecords[2*i];
      sndPack[k].intData[icount[k]++]=integerRecords[2*i+1];
      for(j=0;j<nvar;j++)
	sndPack[k].realData[dcount[k]++]=realRecords[m++];
    }
  //
  // communicate the data across
  //
  pc->sendRecvPackets(sndPack,rcvPack);
  //
  // decode the packets and update the data
  //
  qtmp=(double *)malloc(sizeof(double)*nvar*mb->ntotalPoints);
  itmp=(int *) malloc(sizeof(int)*mb->ntotalPoints);
  for(i=0;i<mb->ntotalPoints;i++) itmp[i]=0;
  //
  for(k=0;k<nrecv;k++)
    {
      m=0;
      for(i=0;i<rcvPack[k].nints;i++)
	{
	  for(j=0;j<nvar;j++)
	    {
	      itmp[rcvPack[k].intData[i]]=1;
	      qtmp[rcvPack[k].intData[i]*nvar+j]=rcvPack[k].realData[m];
	      m++;
	    }
         //if (myid==12) printf("rcvPack[k].intData[i]=%d %d\n",rcvPack[k].intData[i],mb->ntotalPoints);
	}
    }
  norphanPoint=0;
  for(i=0;i<mb->ntotalPoints;i++)
    {
      if (itmp[i]==0) {
	if (fp==NULL) 
	  {
	    sprintf(ofname,"orphan%d.dat",myid);
	    fp=fopen(ofname,"w");
	  }
	mb->outputOrphan(fp,i);
	norphanPoint++;
      }
    }
  if (fp!=NULL) fclose(fp);
  if (norphanPoint > 0 && iorphanPrint) {
   printf("Warning::number of orphans in %d = %d of %d\n",myid,norphanPoint,
	mb->ntotalPoints);
    iorphanPrint=0;
   }
  //
  mb->updatePointData(q,qtmp,nvar,interptype);
  //
  // release all memory
  //
  pc->clearPackets(sndPack,rcvPack);
  free(sndPack);
  free(rcvPack);
  free(qtmp);
  free(itmp);
  if (integerRecords) free(integerRecords);
  if (realRecords) free(realRecords);
  if (icount) free(icount);
  if (dcount) free(dcount);
}
