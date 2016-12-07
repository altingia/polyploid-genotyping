#include <vector>
#include <string>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <stdlib.h>

#include "ModelGeneric.hpp"
#include "ModelDiseq.hpp"
#include "MbRandom.hpp"
#include "main.hpp"

// Declare static member variable
std::vector<double> ModelDiseq::_phi, ModelDiseq::_perIndLogLik, ModelDiseq::_theta0Ind,
                    ModelDiseq::_theta1Ind, ModelDiseq::_theta2Ind, ModelDiseq::_thetaPrimeInd,
                    ModelDiseq::_prevPhi, ModelDiseq::_rInd, ModelDiseq::_vInd;
int ModelDiseq::_currInd;
std::vector<bool> ModelDiseq::_convergedInd, ModelDiseq::_convergedLoci;

ModelDiseq::ModelDiseq(int ac, char* av[]){

  _prefix = "diseq";

  for(int i = 2; i < ac; i++){

    if(strcmp(av[i],"--num-ind") == 0 || strcmp(av[i], "-n") == 0){
      _nInd = atoi(av[i + 1]);
    } else if(strcmp(av[i],"--num-loci") == 0 || strcmp(av[i], "-l") == 0){
      _nLoci = atoi(av[i + 1]);
    } else if(strcmp(av[i],"--ploidy") == 0 || strcmp(av[i], "-p") == 0){
      _ploidy = atoi(av[i + 1]);
    } else if(strcmp(av[i],"--total-reads") == 0 || strcmp(av[i], "-t") == 0){
      _totalReadsFile = av[i + 1];
    } else if(strcmp(av[i],"--ref-reads") == 0 || strcmp(av[i], "-r") == 0){
      _refReadsFile = av[i + 1];
    } else if(strcmp(av[i],"--prefix") == 0){
      _prefix = av[i + 1];
    } else if(strcmp(av[i],"--error-rates") == 0 || strcmp(av[i], "-e") == 0) {
      _errorRatesFile = av[i + 1];
    } else if(strcmp(av[i],"--tol") == 0){
      _tol = atof(av[i + 1]);
    } else if(strcmp(av[i],"--iters") == 0){
      _maxIters = atoi(av[i + 1]);
    } else if(strcmp(av[i],"--stop") == 0){
      _stopVal = atof(av[i+1]);
    } else if(strcmp(av[i],"--quiet") == 0 || strcmp(av[i], "-q") == 0){
      _quiet = 1;
    }

  }

  // Check the input for necessary arguments
  checkCommandLine();

  // Read in the data
  getData();

  // Check data
  checkInput();

  // Initialize the parameters for the ECM algorithm
  initParams();

}

void ModelDiseq::ecm(){

  double currLogLik = 0.0, prevLogLik = 0.0;
  bool convergence = 0;

  for(int j = 0; j < _maxIters; j++){

    prevLogLik = currLogLik;

    if(!_quiet)
      std::cerr << "Step: " << j + 1 << "\t";

    eStep();
    mStep();

    /*if(j >= 10){
      std::cerr << "(accel) ";
      mStepAccel();
    } else {
      mStep();
    }*/

    currLogLik = calcLogLik();

    if(!_quiet)
      std::cerr << std::setprecision(16) << "logLik: " << currLogLik << "\t" << "Diff: " << currLogLik - prevLogLik << std::endl;

    checkConvergence(convergence);

    if(convergence|| fabs(currLogLik - prevLogLik) < 1e-8)
      break;

  }
}

void ModelDiseq::checkConvergence(bool &con){

  int convergedLociCount = 0, convergedIndCount = 0;


  for(int l = 0; l < _nLoci; l++){

    if(_convergedLoci[l])
      convergedLociCount++;

  }

  for(int i = 0; i < _nInd; i++){

    if(_convergedInd[i])
      convergedIndCount++;

  }

  if(convergedLociCount == _nLoci && convergedIndCount == _nInd)
    con = 1;

}

void ModelDiseq::printOutput(){

  std::string genosFile = _prefix + "-genos.txt";
  std::string freqsFile = _prefix + "-freqs.txt";
  std::string phiFile = _prefix + "-F.txt";

  std::ofstream genosStream;
  genosStream.open(genosFile, std::ios::out);

  std::ofstream freqsStream;
  freqsStream.open(freqsFile, std::ios::out);

  std::ofstream phiStream;
  phiStream.open(phiFile, std::ios::out);

  std::vector<double> tmp_val(_ploidy+1, 0.0), g_post_prob(_ploidy+1, 0.0);
  double tmp_val_sum = 0.0, phiConverted = 0.0;
  std::vector<int> genotypes(_nInd * _nLoci, -9);
  int i3d = 0, i2d = 0;

  for(int l = 0; l < _nLoci; l++){
    for(int i = 0; i < _nInd; i++){

      i2d = i * _nLoci + l;

      // Catch missing data and skip
      if(_totReads[i2d] == MISSING)
        continue;

      tmp_val_sum = 0.0;
      phiConverted = 1.0 / _phi[i] - 1.0;

      for(int a = 0; a <= _ploidy; a++){
        i3d = l * _nInd * (_ploidy + 1) + i * (_ploidy + 1) + a;
        tmp_val[a] = _gLiks[i3d] * r->betaBinomPdf(_ploidy, a, _freqs[l] * phiConverted, (1 - _freqs[l]) * phiConverted);
        tmp_val_sum += tmp_val[a];
      }

      for(int a = 0; a <= _ploidy; a++){
        g_post_prob[a] = tmp_val[a]/tmp_val_sum;
      }

      genotypes[i2d] = gMax(g_post_prob);

    }
  }

  for(int i = 0; i < _nInd; i++){
    for(int l = 0; l < _nLoci; l++){
      i2d = i * _nLoci + l;
      genosStream << genotypes[i2d] << "\t";
    }
    genosStream << std::endl;
  }

  for(int l = 0; l < _nLoci; l++)
    freqsStream << _freqs[l] << std::endl;

  for(int i = 0; i < _nInd; i++)
    phiStream << _phi[i] << std::endl;

}

/*******************************
 ** Private member functions  **
 *******************************/

void ModelDiseq::checkCommandLine(){

  if(!_quiet)
    std::cerr << "\nChecking command line input...        ";

  int errorCaught = 0;

  if(_nInd < 0){
    std::cerr << "\nMissing or invalid option for -n [--num-ind].\n";
    errorCaught++;
  }

  if(_nLoci < 0){
    std::cerr << "\nMissing or invalid option for -l [--num-loci].\n";
    errorCaught++;
  }

  if(_ploidy < 0){
    std::cerr << "\nMissing or invalid option for -p [--ploidy].\n";
    errorCaught++;
  }

  if(strcmp(_totalReadsFile.c_str(), "none") == 0){
    std::cerr << "\nMissing or invalid option for -t [--total-reads].\n";
    errorCaught++;
  }

  if(strcmp(_refReadsFile.c_str(), "none") == 0){
    std::cerr << "\nMissing or invalid option for -r [--ref-reads].\n";
    errorCaught++;
  }

  if(strcmp(_errorRatesFile.c_str(), "none") == 0) {
    std::cerr << "\nMissing or invalid option for -e [--error-rates].\n";
    errorCaught++;
  }

  if(errorCaught > 0){
    diseqUsage();
    exit(EXIT_FAILURE);
  }

  if(!_quiet)
    std::cerr << "Good" << std::endl;

}

void ModelDiseq::initParams(){

  _gExp.resize(_nLoci * _nInd * (_ploidy + 1));
  _gLiks.resize(_nLoci * _nInd * (_ploidy + 1));
  _freqs.resize(_nLoci);
  _perSiteLogLik.resize(_nLoci);
  _perIndLogLik.resize(_nInd);
  _phi.resize(_nInd);
  _prevPhi.resize(_nInd);
  _convergedInd.resize(_nInd);
  _convergedLoci.resize(_nLoci);

  // Acceleration parameters (not used)
  _theta0Ind.resize(_nInd);
  _theta1Ind.resize(_nInd);
  _theta2Ind.resize(_nInd);
  _theta0Loci.resize(_nLoci);
  _theta1Loci.resize(_nLoci);
  _theta2Loci.resize(_nLoci);
  _rInd.resize(_nInd);
  _rLoci.resize(_nLoci);
  _vInd.resize(_nInd);
  _vLoci.resize(_nLoci);



  int i2d = 0, i3d = 0; // indices for entries in 2D and 3D arrays stored as vectors
  double gEpsilon = 0.0; // error corrected genotype

  for(int l  = 0; l < _nLoci; l++){
    _freqs[l] = r->uniformRv();
    _convergedLoci[l] = 0;
  }

  for(int i = 0; i < _nInd; i++){
    _phi[i] = 0.1 * r->uniformRv() + 0.01;
    _convergedInd[i] = 0;
  }

  for(int l = 0; l < _nLoci; l++){
    for(int i = 0; i < _nInd; i++){
      i2d = i * _nLoci + l;
      for(int a = 0; a <= _ploidy; a++){
        i3d = l * _nInd * (_ploidy + 1) + i * (_ploidy + 1) + a;

        if(_totReads[i2d] == MISSING){
          _gLiks[i3d] = BADLIK;
        } else {
          gEpsilon = f_epsilon(a, _ploidy, _errRates[l]);
          _gLiks[i3d] = r->binomPdf(_totReads[i2d], _refReads[i2d], gEpsilon);
          //std::cerr << gEpsilon << "    " << _gLiks[i3d] << std::endl;
        }

      }
    }
  }

}

double ModelDiseq::calcFreqLogLik(double x){

  int i3d = 0;
  double logLik = 0.0, alpha = 0.0, beta = 0.0, phiConverted = 0;

  for(int i = 0; i < _nInd; i++){

    // Catch missing data and skip
    if(_totReads[i * _nLoci + _currLoc] == MISSING)
      continue;

    phiConverted = 1.0 / _phi[i] - 1.0;

    if(phiConverted > 1000.0)
      phiConverted = 1000.0;

    alpha = x * phiConverted;
    beta  = (1 - x) * phiConverted;


    for(int a = 0; a <= _ploidy; a++){
      i3d = _currLoc * _nInd * (_ploidy + 1) + i * (_ploidy + 1) + a;
      logLik += _gExp[i3d] * (log(_gLiks[i3d]) + r->lnBetaBinomPdf(_ploidy, a, alpha, beta));
      //std::cerr << _currLoc+1 << "\t" << i+1 << "\t" << a << "\t" << _gExp[i3d] << "\t" << r->lnBetaBinomPdf(_ploidy, a, alpha, beta) << std::endl;
    }

  }

  //if(_currLoc == 120)
  //  std::cerr << _currLoc+1 << "\t" << logLik << "\t" << x << std::endl;

  return -logLik;

}

double ModelDiseq::calcPhiLogLik(double x){

  int i3d = 0;
  double logLik = 0.0, alpha = 0.0, beta = 0.0, phiConverted = 1.0 / x - 1.0;

  for(int l = 0; l < _nLoci; l++){

    // Catch missing data and skip
    if(_totReads[_currInd * _nLoci + l] == MISSING)
      continue;

    alpha = _freqs[l] * phiConverted;
    beta  = (1 - _freqs[l]) * phiConverted;

    for(int a = 0; a <= _ploidy; a++){
      i3d = l * _nInd * (_ploidy + 1) + _currInd * (_ploidy + 1) + a;
      logLik += _gExp[i3d] * (log(_gLiks[i3d]) + r->lnBetaBinomPdf(_ploidy, a, alpha, beta));
    }

  }

  return -logLik;

}

void ModelDiseq::eStep(){

  std::vector<double> tmp_exp(_ploidy + 1, 0.0);
  double tmp_exp_sum = 0.0, phiConverted = 0.0;
  int i3d = 0; // index for 3D array stored as vector
  bool print = 0;

  for(int l = 0; l < _nLoci; l++){
    for(int i = 0; i < _nInd; i++){

      // Catch missing data and skip
      if(_totReads[i * _nLoci + l] == MISSING)
        continue;

      tmp_exp_sum = 0.0;
      phiConverted = 1.0 / _phi[i] - 1.0;

      if(phiConverted > 1000)
        phiConverted = 1000.0;

      for(int a = 0; a <= _ploidy; a++){
        i3d = l * _nInd * (_ploidy + 1) + i * (_ploidy + 1) + a;
        tmp_exp[a] = _gLiks[i3d] * r->betaBinomPdf(_ploidy, a, _freqs[l] * phiConverted, (1.0 - _freqs[l]) * phiConverted);
        //std::cerr << tmp_exp[a] << "    " << _gLiks[i3d] << "    " << r->betaBinomPdf(_ploidy, a, _freqs[l] * _phi[i], (1 - _freqs[l]) * _phi[i]) << std::endl;
        tmp_exp_sum += tmp_exp[a];
      }

      for(int a = 0; a <= _ploidy; a++){
        i3d = l * _nInd * (_ploidy + 1) + i * (_ploidy + 1) + a;
        _gExp[i3d] = tmp_exp[a] / tmp_exp_sum;

        if(print){
        std::cout << i+1 << "    " << l+1 << "    "
                  << a << "    " << r->betaBinomPdf(_ploidy, a, _freqs[l] * phiConverted, (1.0 - _freqs[l]) * phiConverted) << "    "
                  << tmp_exp[a] << "    " <<  _gExp[i3d] << "    "
                  << phiConverted << "    " << _freqs[l] << std::endl;
        }

      }

    }
  }

}

void ModelDiseq::mStep(){

  double prev = 0.0;

  for(int l = 0; l < _nLoci; l++){

    if(_convergedLoci[l]){
      continue;
    } else {
      _currLoc = l;
      prev = _freqs[l];
      _perSiteLogLik[l] = local_min(0.0, 1.0, _tol, calcFreqLogLik, _freqs[l]);
    }

    if(sqrt(pow(_freqs[l] - prev, 2)) < _stopVal)
      _convergedLoci[l] = 1;

  }

  for(int i = 0; i < _nInd; i++){

    if(_convergedInd[i]){
      continue;
    } else {
      _currInd = i;
      prev = _phi[i];
      _perIndLogLik[i] = local_min(0.0, 1.0, _tol, calcPhiLogLik, _phi[i]);
    }

    if(sqrt(pow(_phi[i] - prev, 2)) < _stopVal)
      _convergedInd[i] = 1;

  }

}

/*void ModelDiseq::mStepAccel(){

  for(int l = 0; l < _nLoci; l++){

    if(_convergedLoci[l]){
      continue;
    } else {
      _currLoc = l;
      prev = _freqs[l];
      theta0 = _freqs[l];
      theta1 = theta0;
      _perSiteLogLik[l] = local_min(0.0, 1.0, _tol, calcFreqLogLik, theta1);
      theta2 = theta1;
      eStep();
      _perSiteLogLik[l] = local_min(0.0, 1.0, _tol, calcFreqLogLik, theta2);
      r = theta1 - theta0;
      v = (theta2 - theta1) - r;
      alpha = - sqrt(pow(r, 2)) / sqrt(pow(v, 2));
      theta_prime = theta0 - 2.0 * alpha * r + pow(alpha, 2) * v;
      eStep();
      _perSiteLogLik[l] = local_min(0.0, 1.0, _tol, calcFreqLogLik, theta_prime);
      _freqs[l] = theta_prime;
    }

    if(sqrt(pow(_freqs[l] - prev, 2)) < _stopVal)
      _convergedLoci[l] = 1;

  }

  for(int i = 0; i < _nInd; i++){

    if(_convergedInd[i]){
      continue;
    } else {
      _currInd = i;
      prev = _phi[i];
      theta0 = _phi[i];
      theta1 = theta0;
      _perIndLogLik[i] = local_min(0.0, 1.0, _tol, calcPhiLogLik, theta1);
      theta2 = theta1;
      _perIndLogLik[i] = local_min(0.0, 1.0, _tol, calcPhiLogLik, theta2);
      r = theta1 - theta0;
      v = (theta2 - theta1) - r;
      alpha = - sqrt(pow(r, 2)) / sqrt(pow(v, 2));
      theta_prime = theta0 - 2.0 * alpha * r + pow(alpha, 2) * v;
      _perIndLogLik[i] = local_min(0.0, 1.0, _tol, calcPhiLogLik, theta_prime);
      _phi[i] = theta_prime;
    }

    if(sqrt(pow(_phi[i] - prev, 2)) < _stopVal)
      _convergedInd[i] = 1;

  }

}*/

double ModelDiseq::calcLogLik(){

  int i3d = 0;
  double logLik = 0.0, tmpLik = 0.0, alpha = 0.0, beta = 0.0, phiConverted = 0.0;

  for(int l = 0; l < _nLoci; l++){
    for(int i = 0; i < _nInd; i++){

      // Catch missing data and skip
      if(_totReads[i * _nLoci + l] == MISSING)
        continue;

      phiConverted = 1.0 / _phi[i] - 1.0;

      if(phiConverted > 1000.0)
        phiConverted = 1000.0;

      alpha = _freqs[l] * phiConverted;
      beta  = (1 - _freqs[l]) * phiConverted;
      tmpLik = 0.0;
      for(int a = 0; a <= _ploidy; a++){
        i3d = l * _nInd * (_ploidy + 1) + i * (_ploidy + 1) + a;
        tmpLik += _gLiks[i3d] * r->betaBinomPdf(_ploidy, a, alpha, beta);
      }

      logLik += log(tmpLik);

    }
  }

  return logLik;

}
