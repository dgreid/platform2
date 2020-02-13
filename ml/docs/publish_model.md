# ChromeOS ML Service: How to publish your ML models

This page explains how to publish a trained ML model so that the ML Service
can make it available at runtime for inference.
Design doc for model publishing: [go/cros-ml-service-models].

[TOC]

## Model format

Currently the ML Service supports **TensorFlow Lite** models. Each model needs
to be a single file. The file format is a TFLite flat buffers (extension
.tflite).

You can convert a TF model into a TFLite model using the [TensorFlow Lite
Converter (Toco)][toco] tool.

Google-specific: For resources about the overall Chrome OS ML model training &
deployment lifecycle across google3, Chromium, and Chrome OS, see [go/ml-abc].

### TFLite runtime version

You need to be aware what (minimum) version of the TFLite runtime your model
requires.
> TODO(amoylan): Show an example of how to verify this using Toco.

The following table shows the version of ML Service's TFLite runtime for each
CrOS major version:

| CrOS version    | TFLite version   |
|:---------------:|:----------------:|
| M71 (and prior) | (none)           |
| M72             | 1.9.0            |
| M73             | 1.9.0            |
| M74             | 1.9.0            |
| M75             | 1.9.0            |
| M76             | 1.9.0            |
| M77             | 1.9.0            |
| M78             | 1.14.0           |
| M79             | 1.14.0           |
| M80             | 1.14.0           |
| M81             | 1.14.0           |


## Two methods to publish your models

Two methods will be supported to publish an ML model:

1. Installed into rootfs partition.
2. Installed into the user partition as a downloadable component using Chrome
   component updater. (Under development, ETA 2020-Q1).

To know more about why we have two methods and the pros and cons of each, read
the [section below](#two-methods-why).

## Method #1: Install a model inside rootfs

### Step 1. Upload the model to the ChromeOS file mirror

Once you have a model ready that you want to publish, upload it to
https://storage.googleapis.com/chromeos-localmirror/distfiles/.
The web interface is
https://pantheon.corp.google.com/storage/browser/chromeos-localmirror/distfiles.
The mirror is accessible to all Googlers with prod access.

There is no directory structure under `distfiles/`, so the filename needs to be
unique enough to avoid conflicts.
This is the **required convention** for filenames of models:

```
mlservice-model-<feature_name_and_variant>-<timestamp_and_version>.<ext>
```

* `mlservice-model` is a fixed prefix
* `<feature_name_and_variant>` should indicate the feature that this model is
  built for and any variant of the model if applicable
* `<timestamp_and_version>` is used to differentiate different versions of the
  same model. The preferred format is `20180725`. To disambiguate further, the
  actual time (hour, minutes, etc) can be added, or you can add a version string
  like `-v2`.

An example of filename is:
`mlservice-model-tab_discarder_quantized-20180507-v2.tflite`.

After you upload the file, make it publicly visible by selecting Edit
Permissions and adding a 'Reader' permission for a Group named 'allUsers'.

Files in the ChromeOS file mirror should never be deleted. You simply add newer
models as you need them, but leave the previous ones in the mirror, even if you
don't plan to use them ever again.

### Step 2. Write an ebuild to install the model into rootfs

Once your model is uploaded to the ChromeOS file mirror, the next step is to
create a CL.
This CL will have changes to the [ML Service ebuild], which installs the
model(s) into rootfs.
The ebuild is located at
`/chromiumos/overlays/chromiumos-overlay/chromeos-base/ml/ml-9999.ebuild`.
Simply add your models in the `system_models` variable: they are installed in the
`src_install()` function in the same file.
The install location in the ChromeOS system is `/opt/google/chrome/ml_models`.

Then you need to update the Manifest file. In the ebuild path above, run
`ebuild <ebuild_file> manifest`.
A new entry for the new model will be added into Manifest file. This is the
checksum to ensure the model file is downloaded successfully.

See [CL/1125701] (and relative fix in [CL/1140020]) as an example.

### Step 3. Update ML Service daemon to serve the new model

There are three places to update here:

1. Add the model to the Mojo interface in [model.mojom].
2. Add a metadata entry to [model_metadata.cc].
3. Add a basic [loading & inference test] for the model.

See [this CL](https://crrev.com/c/1342736) for an example of all the above.

### Step 4. Log the model.

1. Specify |metrics_base_name| for your model in [model_metadata.cc].
2. Update the "MachineLearningServiceModels" histogram_suffixes entry in
   [histograms.xml].

See the [logging section](#log-your-model-on-uma) for more details.

### Upgrading to a new version of a rootfs model

If you have a new, probably better version of a model, these are the recommended
steps to swap it in:

1. Chrome OS: Add it as a separate model using the instructions above, with a
   timestamped name like MY_MODEL_201905.
2. Chrome: [Add a feature flag][add-feature-flag] and use it to toggle between
   the old & new versions of the model.
   * If any Chrome-side pre-processing or post-processing code is
     model-dependent, this should also be toggled based on this feature flag.
   * Consider updating your existing unit tests to run against both versions of
     the model, by using TEST_P.
3. Finch: Launch the new feature according to the instructions at
   [go/finch-best-practices]. Those instructions will take you all the way
   through to removing the feature flag you added above.
4. Chrome OS: Mark the old model MY_MODEL_OLD as unsupported:
   1. Rename the old model to UNSUPPORTED_MY_MODEL_OLD in [model.mojom].
   2. Remove the model from [model_metadata.cc], and remove its [loading &
      inference test].
   3. Update the [ML Service ebuild] to no longer install the model file onto
      rootfs. Remove it from the Manifest file in the same directory.

If the model you are replacing hasn't launched yet, you can of course skip the
Finch rollout. Optionally, you can also take this shortcut (with caveats):

* Upload a new .tflite file, update the [ML Service ebuild], and then directly
  change [model_metadata.cc] to point the existing model at that file, instead
  of adding a new model ID. This will silently swap the model (from Chrome's
  POV).
* If there is any Chrome-side logic that depends on the specific version of the
  model (such as pre-processing config), it will break. You can update Chrome at
  the "same" time, but it will be a day or three before Chrome is up-revved into
  Chrome OS and everything is back in sync. It's to you if breaking your
  unlaunched feature for a few days is OK. (Don't cause Chrome to start crashing
  of course.)



## Method #2: Use component updater to install a model inside the stateful partition

> Detailed instructions for this section are pending the final implementation of
> OOB model updates. See [go/cros-model-update] for the design. ETA 2020-Q1.


## Which method to use and why {#two-methods-why}
We provide two methods of model deployment, each with their own pros and cons.

Method #1, installing inside rootfs, has the benefit of making the model
available right after the system boots for the first time.  No updates and thus
no network connection is required.  Besides, it was better suited as a starting
point to create a working prototype of the ML Service in the early stages of
development.

Method #1 has one big limit: storage space on rootfs. Since we want to avoid
increasing the size of rootfs, we reserve this method for those models that are
either very small (less than 30KB) or that are system critical and need to be
available before the first successful update of the device.

A second property of Method #1 is that it ties model versions to the platform
version, i.e. updated every 6 weeks after passing through dev and Beta versions.

Method #2 is the solution to the limitations of Method #1. Large models can be
installed this way, because we have fewer restrictions on the size increase of
the stateful partition (within reason); and the component updater approach
allows the rollout of models (controlled by e.g. Finch) in between platform
releases.

The disadvantage of Method #2 is that the model cannot be used until
the component updater has fetched the component, i.e. it won't be available
out-of-box. For models that need to be present when the device is first used or
otherwise always be available, a combination of Methods #1 and #2 will be
needed.

Contact chrome-knowledge-eng@ to consult about the right approach.


## Log your model on UMA {#log-your-model-on-uma}

### Introduction to the metrics
There are 9 "request performance metric" histograms and 3 “request event” enums
histograms that model owner MUST config to be logged on go/uma-.

The name format of the 9 "request performance metric" histograms is,

MachineLearningService.**MetricModelName**.**RequestEventName**.**ResourceName**

**MetricModelName** is defined by specifying |metrics_model_name|
in [model_metadata.cc].

There are 3 different **RequestEventName** corresponding to 3 types of
requests (e.g. from Chrome):

1. LoadModelResult: request to load your model.
2. CreateGraphExecutorResult: request to create graph executor for your model.
3. ExecuteResult: request to execute your model.

For each request, there are two **ResourceName** corresponding to two computer
resources:

1. CpuTimeMicrosec: cpu time in microseconds used in handling the request.
2. TotalMemoryDeltaKb: memory usage incurred by handling the request in Kb.


For the enum histograms, the name format is,

MachineLearningService.**MetricModelName**.**RequestEventName**.Event

where **MetricModelName** and **RequestEventName** are same as defined above.

### Enable the logging

There are two tasks to enable the logging:

* (In a ChromeOS CL) Specify |metrics_base_name| for your model in
  [model_metadata.cc]. Please note that |metrics_base_name| must NOT be empty.
  And models with the same **MetricModelName** will be logged exactly in the
  same histogram, even if their model ids are different. (This lets you compare
  alternative versions of a given model in Finch A/B experiments.)
* (In a Chromium CL) Update [histograms.xml]. You need to add a new suffix
  including your **MetricModelName** and a brief label of your model to
   ```<histogram_suffixes name="MachineLearningServiceModels" separator="."></>```.
  You can see the suffixes of "SmartDimModel" or "TestModel" for examples.
  Please note that, after modifying [histograms.xml], one has to use
  [pretty_print.py] and [validate_format.py] to validate the format of
  [histograms.xml] before uploading your CL. Both of the scripts locate in the
  same folder of [histograms.xml] and can be directly run without arguments.
  [pretty_print.py] can also help you justify the format.

In reviewing the CL of modifying histograms.xml, you need to specify one
reviewer from the owners of histograms.xml. The owners can be found at
[metrics-owner].

### Further readings on histograms.xml
1. [Histograms readme][hist-readme]
2. [Histograms one-pager][hist-one-pager]


[add-feature-flag]: https://chromium.googlesource.com/chromium/src/+/HEAD/docs/how_to_add_your_feature_flag.md
[CL/1125701]: http://crrev.com/c/1125701
[CL/1140020]: http://crrev.com/c/1140020
[go/cros-ml-service-models]: http://go/cros-ml-service-models
[go/cros-model-update]: http://go/cros-model-update
[go/finch-best-practices]: http://go/finch-best-practices
[go/ml-abc]: http://go/ml-abc
[hist-one-pager]: https://chromium.googlesource.com/chromium/src/tools/+/refs/heads/master/metrics/histograms/one-pager.md
[hist-readme]: https://chromium.googlesource.com/chromium/src/tools/+/refs/heads/master/metrics/histograms/README.md
[histograms.xml]: https://cs.chromium.org/chromium/src/tools/metrics/histograms/histograms.xml?q=histograms.xml&sq=package:chromium&dr
[loading & inference test]: https://cs.corp.google.com/chromeos_public/src/platform2/ml/machine_learning_service_impl_test.cc
[metrics-owner]: https://cs.chromium.org/chromium/src/base/metrics/OWNERS?q=base/metrics/OWNERS&sq=package:chromium&dr
[ML Service ebuild]: https://cs.corp.google.com/chromeos_public/src/third_party/chromiumos-overlay/chromeos-base/ml/ml-9999.ebuild
[model.mojom]: https://cs.corp.google.com/chromeos_public/src/platform2/ml/mojom/model.mojom
[model_metadata.cc]: https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/ml/model_metadata.cc
[pretty_print.py]: https://cs.chromium.org/chromium/src/tools/metrics/histograms/pretty_print.py?sq=package:chromium&dr&g=0
[toco]: https://github.com/tensorflow/tensorflow/tree/master/tensorflow/lite/toco
[validate_format.py]: https://cs.chromium.org/chromium/src/tools/metrics/histograms/validate_format.py?sq=package:chromium&dr&g=0
