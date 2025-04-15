#!/usr/bin/awk

BEGIN {
    audio_pipeline_resume = 0
    corrupt_head = 0
    error = 0
    load_prohibited = 0
    min_free_iram = 0
    min_free_spiram = 0
    not_in_speeching = 0
    result_ok_false = 0
    result_ok_true = 0
    runtime_max = 0
    runtime_min = 1000000000
    runtime_total = 0
    stack_overflow = 0
    store_prohibited = 0
    temperature = 0
    vad_end = 0
    vad_start = 0
    wakeup_end = 0
    wakeup_start = 0
}

{
    # remove parentheses around timestamp
    gsub(/[()]/, "", $2);

    # save complete timestamp
    ts = $2

    # extract seconds.milliseconds from timestamp
    gsub(/[[:digit:]]{2}:[[:digit:]]{2}:/, "", $2);

    if (/"result":{"ok":false/)      result_ok_false++;
    if (/"result":{"ok":true/) {
        result_ok_true++;

        end = ts
        end_sec = $2

        runtime = end_sec - start_sec

        # end happens next minute
        if (runtime < 0)
            runtime += 60

        # convert to ms
        runtime *= 1000

        runtime_total += runtime

        if (runtime > runtime_max)
            runtime_max = runtime

        if (runtime < runtime_min)
            runtime_min = runtime
    }
    if (/audio_pipeline_resume/)    audio_pipeline_resume++;
    if (/CORRUPT HEAP/)             corrupt_heap++;
    if (/error/)                    error++;
    if (/LoadProhibited/)           load_prohibited++;
    if (/Not in speeching/)         not_in_speeching++;
    if (/stack overflow/)           stack_overflow++;
    if (/StoreProhibited/)          store_prohibited++;
    if (/AUDIO_REC_VAD_END/)        vad_end++;

    if (/AUDIO_REC_VAD_START/)      vad_start++;
    if (/AUDIO_REC_WAKEUP_END/) {
        wakeup_end++;

        start = ts
        start_sec = $2
    }
    if (/AUDIO_REC_WAKEUP_START/)   wakeup_start++;

    # CONFIG_WILLOW_DEBUG_MEM
    # if (/0x3d800000/)               spiram_min_free = $10

    if (/min_free_iram/)            min_free_iram = $5;
    if (/min_free_spiram/)          min_free_spiram = $5;
    if (/temperature/) {
        gsub(/\033\[[0-9;]*[a-zA-Z]/, "", $6);
        temp = $6 + 0;
        if (temp > temperature) {
            temperature = temp;
        }
    }

}

END {
    printf "AUDIO_REC_WAKEUP_START: %d\n",  wakeup_start;
    printf "AUDIO_REC_VAD_START: %d\n",     vad_start;
    printf "AUDIO_REC_VAD_END: %d\n",       vad_end;
    printf "AUDIO_REC_WAKEUP_END: %d\n",    wakeup_end;
    printf "audio_pipeline_resume: %d\n", audio_pipeline_resume;
    printf "error: %d\n", error;
    printf "Not in speeching: %d\n", not_in_speeching;
    printf "result_ok_false: %d\n", result_ok_false;
    printf "result_ok_true: %d\n", result_ok_true;
    printf "\n";

    printf "min_free_iram: %d\n", min_free_iram;
    printf "min_free_spiram: %d\n", min_free_spiram;
    printf "temperature: %.02fC\n", temperature;
    printf "\n";

    printf "heap corruption: %d\n", corrupt_heap;
    printf "load prohibited: %d\n", load_prohibited;
    printf "stack overflow: %d\n", stack_overflow;
    printf "store prohibited: %d\n", store_prohibited;
    printf "\n";

    if (result_ok_true > 0) {
        printf "average wake end to result: %d\n", runtime_total / result_ok_true
        printf "fastest wake end to result: %d\n", runtime_min;
        printf "slowest wake end to result: %d\n", runtime_max;
    }
}
