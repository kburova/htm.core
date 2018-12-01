// Written by David McDougall, 2018
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iostream>
#include <vector>
#include <random>

#include "nupic/algorithms/SpatialPooler.hpp"
#include <nupic/algorithms/SDRClassifier.hpp>
#include <nupic/algorithms/ClassifierResult.hpp>

using namespace std;
using namespace nupic;
using nupic::algorithms::spatial_pooler::SpatialPoolerTopology;
using nupic::algorithms::sdr_classifier::SDRClassifier;
using nupic::algorithms::cla_classifier::ClassifierResult;


#include <stdio.h>
#include <execinfo.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
void keyboard_interrupt_handler(int sig) {
  void *array[50];
  size_t size;

  // get void*'s for all entries on the stack
  size = backtrace(array, 50);

  // print out all the frames to stderr
  fprintf(stderr, "Error: signal %d:\n", sig);
  backtrace_symbols_fd(array, size, STDERR_FILENO);
  exit(1);
}



vector<UInt> read_mnist_labels(string path) {
    ifstream file(path);
    if( !file.is_open() ) {
        cerr << "ERROR: Failed to open file " << path << endl;
        exit(1);
    }
    int magic_number     = 0;
    int number_of_labels = 0;
    file.read( (char*) &magic_number,     4);
    file.read( (char*) &number_of_labels, 4);
    if(magic_number != 0x00000801) {
        std::reverse((char*) &magic_number,      (char*) &magic_number + 4);
        std::reverse((char*) &number_of_labels,  (char*) &number_of_labels + 4);
    }
    if(magic_number != 0x00000801) {
        cerr << "ERROR: MNIST data is compressed or corrupt" << endl;
        exit(1);
    }
    vector<UInt> retval;
    for(int i = 0; i < number_of_labels; ++i) {
        unsigned char label = 0;
        file.read( (char*) &label, 1);
        retval.push_back((UInt) label);
    }
    return retval;
}

vector<UInt*> read_mnist_images(string path) {
    ifstream file(path);
    if( !file.is_open() ) {
        cerr << "ERROR: Failed to open file " << path << endl;
        exit(1);
    }
    int magic_number     = 0;
    int number_of_images = 0;
    int n_rows           = 0;
    int n_cols           = 0;
    file.read( (char*) &magic_number,     4);
    file.read( (char*) &number_of_images, 4);
    file.read( (char*) &n_rows,           4);
    file.read( (char*) &n_cols,           4);
    if(magic_number != 0x00000803) {
        std::reverse((char*) &magic_number,      (char*) &magic_number + 4);
        std::reverse((char*) &number_of_images,  (char*) &number_of_images + 4);
        std::reverse((char*) &n_rows,            (char*) &n_rows + 4);
        std::reverse((char*) &n_cols,            (char*) &n_cols + 4);
    }
    if(magic_number != 0x00000803) {
        cerr << "ERROR: MNIST data is compressed or corrupt" << endl;
        exit(1);
    }
    NTA_ASSERT(n_rows == 28);
    NTA_ASSERT(n_cols == 28);
    UInt img_size = n_rows * n_cols;
    vector<UInt*> retval;
    for(int i = 0; i < number_of_images; ++i) {
        auto data_raw = new unsigned char[img_size];
        file.read( (char*) data_raw, img_size);
        // Copy the data into an array of UInt's
        auto data = new UInt[2 * img_size];
        // Apply a threshold to the image, yielding a B & W image.
        for(UInt pixel = 0; pixel < img_size; pixel++) {
            data[2 * pixel] = data_raw[pixel] >= 128 ? 1 : 0;
            data[2 * pixel + 1] = 1 - data[2 * pixel];
        }
        retval.push_back(data);
        delete[] data_raw;
    }
    return retval;
}


int main(int argc, const char *argv[]) {
  // TODO: arg for train time, verbosity
  UInt train_time = 60000 * 1;
  UInt verbosity = 1;

  signal(SIGINT, keyboard_interrupt_handler);   // install stack trace printout on error
  signal(SIGSEGV, keyboard_interrupt_handler);   // install stack trace printout on error

   // SpatialPoolerTopology sp(
   //   /* inputDimensions */                 {28, 28, 2},
   //   /* macroColumnDimensions */           {10, 10, 1},
   //   /* miniColumns */                     100,
   //   /* potentialRadius */                 {3.6, 3.6 , 999.},
   //   /* potentialPct */                    1.00,
   //   /* localAreaDensity */                .014091711495013154,
   //   /* numActiveColumnsPerInhArea */      -1,
   //   /* stimulusThreshold */               28,
   //   /* synPermInactiveDec */              .009282175512186927,
   //   /* synPermActiveInc */                .032294958166728734,
   //   /* synPermConnected */                .4222038828631455,
   //   /* minPctOverlapDutyCycles */         0, // .00071333,
   //   /* dutyCyclePeriod */                 1. / 0.0007133352820011685,
   //   /* boostStrength */                   0.,
   //   /* seed */                            0,
   //   /* spVerbosity */                     verbosity
   //   /* wrapAround */                      );

  SpatialPoolerTopology sp(
    /* numInputs */                    {28, 28, 2},
    /* numColumns */                   {10, 10, 100},
    /* potentialRadius */              3.6,
    /* potentialPct */                 .95,
    /* globalInhibition */             1,
    /* localAreaDensity */             .016,
    /* numActiveColumnsPerInhArea */   -1,
    /* stimulusThreshold */             28,
    /* synPermInactiveDec */           .00928,
    /* synPermActiveInc */             .032,
    /* synPermConnected */             .422,
    /* minPctOverlapDutyCycles */      0.,
    /* dutyCyclePeriod */              1400,
    /* boostStrength */                0,
    /* CPP SP seed */                  93,
    /* spVerbosity */                  verbosity,
    /* wrapAround */                   1
    );
  int ncols = 10 * 10 * 100;

  SDRClassifier clsr(
    /* steps */         {0},
    /* alpha */         .001,
    /* actValueAlpha */ .3,
                        verbosity);

  UInt recordNum   = 0;
  auto activeArray = vector<UInt>(ncols, 0);

  // Train
  auto train_images = read_mnist_images("./mnist_data/train-images-idx3-ubyte");
  auto train_labels = read_mnist_labels("./mnist_data/train-labels-idx1-ubyte");
  if(verbosity)
    cout << "Training for " << train_time << " cycles ..." << endl;
  for(UInt i = 0; i < train_time; i++) {
      // Get the input & label
      UInt index  = rand() % train_labels.size();
      UInt *image = train_images[index];
      UInt label  = train_labels[index];

      // Compute & Train
      sp.compute(image, true, activeArray.data());
      vector<UInt> activeIndex;
      for(UInt i = 0; i < activeArray.size(); i++) {
        if( activeArray[i] ) {
          activeIndex.push_back(i);
        }
      }
      ClassifierResult result;
      clsr.compute(recordNum++, activeIndex,
        /* bucketIdxList */   {label},
        /* actValueList */    {(Real)label},
        /* category */        true,
        /* learn */           true,
        /* infer */           false,
                              &result);
      // cout << "." << flush;
  }

  // Test
  auto test_images  = read_mnist_images("./mnist_data/t10k-images-idx3-ubyte");
  auto test_labels  = read_mnist_labels("./mnist_data/t10k-labels-idx1-ubyte");
  Real score = 0;
  UInt n_samples = 0;
  if(verbosity)
    cout << "Testing for " << test_labels.size() << " cycles ..." << endl;
  for(UInt i = 0; i < test_labels.size(); i++) {
    // Get the input & label
    UInt *image = test_images[i];
    UInt label  = test_labels[i];

    // Compute
    auto activeArray = vector<UInt>( ncols, 0);
    sp.compute(image, false, activeArray.data());
    vector<UInt> activeIndex;
    for(UInt i = 0; i < activeArray.size(); i++) {
      if( activeArray[i] ) {
        activeIndex.push_back(i);
      }
    }
    ClassifierResult result;
    clsr.compute(recordNum++, activeIndex,
      /* bucketIdxList */   {},
      /* actValueList */    {},
      /* category */        true,
      /* learn */           false,
      /* infer */           true,
                            &result);
    // Check results
    for(auto iter : result) {
      if( iter.first == 0 ) {
          auto *pdf = iter.second;
          auto max  = std::max_element(pdf->begin(), pdf->end());
          UInt cls  = max - pdf->begin();
          if(cls == label)
            score += 1;
          n_samples += 1;
      }
    }
  }
  cout << "Score: " << score / n_samples << endl;
}
