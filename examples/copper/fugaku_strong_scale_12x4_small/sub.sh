#!/bin/bash

pjsub run_compress_preprocess_12x4x5_1008x5.sh
pjsub run_compress_preprocess_12x4x11_1008x5.sh
pjsub run_compress_preprocess_12x4x25_1008x5.sh
pjsub run_compress_preprocess_12x4x57_1008x5.sh
pjsub run_compress_preprocess_12x4x114_1008x5.sh
pjsub run_compress_preprocess_12x4x228_1008x5.sh
pjsub run_compress_preprocess_12x4x456_1008x5.sh


watch pjstat