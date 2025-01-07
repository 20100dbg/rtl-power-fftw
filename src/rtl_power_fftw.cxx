#include <iostream>
#include <fstream>
#include <rtl-sdr.h>

#include "acquisition.h"
#include "datastore.h"
#include "device.h"
#include "exceptions.h"
#include "interrupts.h"
#include "params.h"
#include "metadata.h"

#include <time.h>

std::ofstream binfile, metafile;

int metaRows = 1;
int metaCols = 0;
float avgScanDur = 0.0;
float sumScanDur = 0.0;
time_t scanEnd, scanBeg;
int tunfreq;

int startFreq, endFreq, stepFreq;
std::string firstAcqTimestamp, lastAcqTimestamp;
int cntTimeStamps;

int main(int argc, char **argv)
{
  bool do_exit = false;

  ReturnValue final_retval = ReturnValue::Success;

  Params params(argc, argv);
  //params.session_duration_isSet = false;
  
  AuxData auxData(params);
  Rtlsdr rtldev(params.dev_index);

  
  // Print the available gains and select the one nearest to the requested gain.
  //rtldev.print_gains();
  int gain = rtldev.nearest_gain(params.gain);
  rtldev.set_gain(gain);

  // Temporarily set the frequency to params.cfreq, just so that the device does not
  // complain upon setting the sample rate. If this fails, it's not a big deal:
  rtldev.set_frequency(params.cfreq);
  /*
  try {
    rtldev.set_frequency(params.cfreq);
  }
  catch (RPFexception&) {}
  */

  // Set frequency correction
  if (params.ppm_error != 0) {
    rtldev.set_freq_correction(params.ppm_error);
  }

  // Set sample rate
  rtldev.set_sample_rate(params.sample_rate);
  int actual_samplerate = rtldev.sample_rate();

  // Create a plan of the operation. This will calculate the number of repeats,
  // adjust the buffer size for optimal performance and create a list of frequency hops.
  Plan plan(params, actual_samplerate);
  // Print info on capture time and associated specifics.
  plan.print();

  //Begin the work: prepare data buffers
  Datastore data(params, auxData.window_values);

  // Install a signal handler for detecting Ctrl+C.
  set_CtrlC_handler(true);


  params.finalfreq = plan.freqs_to_tune.back();
  //Read from device and do FFT

  do
  {
    for (auto iter = plan.freqs_to_tune.begin(); iter != plan.freqs_to_tune.end();)
    {
      // Begin a new data acquisition.
      Acquisition acquisition(params, auxData, rtldev, data, actual_samplerate, *iter);
      try {
        // Read the required amount of data and process it.
        acquisition.run();
        iter++;
      }
      catch (TuneError &e) {
        // The receiver was unable to tune to this frequency. It might be just a "dead"
        // spot of the receiver. Remove this frequency from the list and continue.
        //iter = plan.freqs_to_tune.erase(iter);
        continue;
      }

      // Write the gathered data to stdout.
      acquisition.get_power();

      // Check for interrupts.
      if (checkInterrupt(InterruptState::FinishNow))
        break;
    }

    if(checkInterrupt(InterruptState::FinishPass)) do_exit = true;

  } while ( !do_exit );

  return (int)final_retval;
}
