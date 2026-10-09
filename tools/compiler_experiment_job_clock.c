// Public Actions occupancy clock transport. No child, network or candidate
// code executes here. Include after native qualification data primitives.
// Trusted API observation is rebound to exact current runtime identity; only
// the authenticated final publisher can establish complete physical occupancy.
#ifndef BUSTER_COMPILER_EXPERIMENT_JOB_CLOCK_INCLUDED
#define BUSTER_COMPILER_EXPERIMENT_JOB_CLOCK_INCLUDED
#if BUSTER_LINUX && !BUSTER_ANDROID
#include <time.h>
#endif

typedef struct CompilerExperimentJobClock CompilerExperimentJobClock;
struct CompilerExperimentJobClock
{
    String8 record, kind, run_id, job_id, runner_name, policy_trusted_revision;
    u64 job_started_us, start_lower_us, observer_started_us, observer_finished_us;
    u64 entry_realtime_us, entry_monotonic_us, entry_elapsed_us;
    bool valid;
};

BUSTER_GLOBAL_LOCAL bool compiler_experiment_job_clock_decimal(String8 text, u64* output)
{
    bool result = text.length && text.length <= 20 && (text.length == 1 || text.pointer[0] != '0');
    u64 value = 0;
    for (u64 i = 0; result && i < text.length; i += 1)
    {
        u8 byte = text.pointer[i];
        result = byte >= '0' && byte <= '9' && value <= (UINT64_MAX-(u64)(byte-'0'))/10;
        if (result) value = value*10+(u64)(byte-'0');
    }
    if (result) *output = value;
    return result;
}

BUSTER_GLOBAL_LOCAL String8 compiler_experiment_job_clock_environment(String8 key, bool* valid)
{
    String8 result = {0};
    u64 count = 0;
    for (u64 i = 0; i < program_state->input.environment_keys.length; i += 1)
        if (string_equal(program_state->input.environment_keys.pointer[i], key))
        {
            count += 1;
            if (i < program_state->input.environment_values.length)
                result = program_state->input.environment_values.pointer[i];
        }
    *valid = count == 1 && result.length;
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_job_clock_matches(String8 key, String8 value)
{
    bool valid = false;
    String8 actual = compiler_experiment_job_clock_environment(key, &valid);
    return valid && string_equal(actual, value);
}

BUSTER_GLOBAL_LOCAL u64 compiler_experiment_job_clock_utc(String8 text)
{
    // Bounded civil-calendar conversion; no locale, time zone, mktime or shell.
    u64 result = 0, year = 0;
    bool valid = compiler_sampling_admission_timestamp(text);
    for (u64 i = 0; valid && i < 4; i += 1) year=year*10+(u64)(text.pointer[i]-'0');
    valid = valid && year >= 2020 && year <= 2100;
    if (valid)
    {
        u64 month=(u64)(text.pointer[5]-'0')*10+(u64)(text.pointer[6]-'0');
        u64 day=(u64)(text.pointer[8]-'0')*10+(u64)(text.pointer[9]-'0');
        u64 hour=(u64)(text.pointer[11]-'0')*10+(u64)(text.pointer[12]-'0');
        u64 minute=(u64)(text.pointer[14]-'0')*10+(u64)(text.pointer[15]-'0');
        u64 second=(u64)(text.pointer[17]-'0')*10+(u64)(text.pointer[18]-'0');
        u64 days = 0;
        for (u64 y = 1970; y < year; y += 1)
            days += 365+((y%4==0 && (y%100!=0 || y%400==0)) ? 1 : 0);
        u64 lengths[]={0,31,28,31,30,31,30,31,31,30,31,30,31};
        if (year%4==0 && (year%100!=0 || year%400==0)) lengths[2]=29;
        for (u64 m=1;m<month;m+=1) days+=lengths[m];
        result = ((days+day-1)*86400+hour*3600+minute*60+second)*1000000ull;
    }
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_job_clock_now(u64* output)
{
    bool result = false;
#if BUSTER_LINUX && !BUSTER_ANDROID
    struct timespec current = {0};
    result = clock_gettime(CLOCK_REALTIME, &current) == 0 && current.tv_sec > 0 && current.tv_nsec >= 0 &&
        current.tv_nsec < 1000000000 && (u64)current.tv_sec <= (UINT64_MAX-999999ull)/1000000ull;
    if (result) *output = (u64)current.tv_sec*1000000ull+(u64)current.tv_nsec/1000ull;
#else
    BUSTER_UNUSED(output);
#endif
    return result;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_job_clock_parse(Arena* arena, String8 record,
    String8 expected_kind, String8 expected_policy, CompilerExperimentJobClock* output)
{
    String8 names[] = {S8("schema"),S8("kind"),S8("repository"),S8("run_id"),S8("run_attempt"),
        S8("policy_trusted_revision"),S8("job_id"),S8("job_name"),S8("runner_id"),S8("runner_name"),
        S8("started_at"),S8("started_unix_us"),S8("start_lower_unix_us"),S8("observer_started_unix_us"),
        S8("observer_finished_unix_us"),S8("observer_monotonic_elapsed_us"),S8("timestamp_precision_us"),S8("observation_scope")};
    String8 values[BUSTER_ARRAY_LENGTH(names)] = {0};
    bool valid = record.length && record.length <= 8192 && record.pointer[record.length-1] == '\n';
    u64 start=0,row=0;
    for (u64 i=0;valid && i<record.length;i+=1)
    {
        u8 byte=record.pointer[i];
        valid = byte=='\n' || byte=='\t' || (byte>=32 && byte<=126);
        if (valid && byte=='\n')
        {
            String8 line=string_slice(record,start,i);
            u64 tab=line.length, tabs=0;
            for (u64 j=0;j<line.length;j+=1) if (line.pointer[j]=='\t') {tab=j;tabs+=1;}
            valid = row<BUSTER_ARRAY_LENGTH(names) && tabs==1 && tab && tab+1<line.length &&
                string_equal(string_slice(line,0,tab),names[row]);
            if (valid) values[row++]=string_slice(line,tab+1,line.length);
            start=i+1;
        }
    }
    valid = valid && row==BUSTER_ARRAY_LENGTH(names) &&
        string_equal(values[0],S8("buster-compiler-physical-job-clock-v1")) &&
        string_equal(values[1],expected_kind) && string_equal(values[2],S8("buster14a/buster")) &&
        string_equal(values[4],S8("1")) && string_equal(values[5],expected_policy) &&
        compiler_sampling_hex(expected_policy,40) &&
        string_equal(values[16],S8("1000000")) && string_equal(values[17],S8("public-platform-job-start"));
    String8 job_name = string_equal(expected_kind,S8("utility")) ? S8("Compiler closure utility") :
        string_equal(expected_kind,S8("preparation")) ? S8("Compiler preparation qualification") :
        string_equal(expected_kind,S8("sampling")) ? S8("Sampling qualification packet") : (String8){0};
    u64 run_id=0,job_id=0,runner_id=0,started=0,lower=0,observer_started=0,observer_finished=0,observed_elapsed=0;
    valid = valid && job_name.length && string_equal(values[7],job_name) &&
        compiler_experiment_job_clock_decimal(values[3],&run_id) && run_id &&
        compiler_experiment_job_clock_decimal(values[6],&job_id) && job_id &&
        compiler_experiment_job_clock_decimal(values[8],&runner_id) && runner_id &&
        compiler_experiment_job_clock_decimal(values[11],&started) && started &&
        compiler_experiment_job_clock_decimal(values[12],&lower) && lower &&
        compiler_experiment_job_clock_decimal(values[13],&observer_started) && observer_started &&
        compiler_experiment_job_clock_decimal(values[14],&observer_finished) && observer_finished &&
        compiler_experiment_job_clock_decimal(values[15],&observed_elapsed) && observed_elapsed<=120000000ull &&
        compiler_experiment_job_clock_utc(values[10])==started && started>1000000ull &&
        lower==started-1000000ull && observer_started>=lower && observer_finished>=observer_started &&
        observer_finished-observer_started<=120000000ull;
    u64 realtime_elapsed=valid ? observer_finished-observer_started : 0;
    u64 disagreement=realtime_elapsed>observed_elapsed ? realtime_elapsed-observed_elapsed : observed_elapsed-realtime_elapsed;
    valid = valid && disagreement<=2000000ull &&
        compiler_experiment_job_clock_matches(S8("GITHUB_REPOSITORY"),values[2]) &&
        compiler_experiment_job_clock_matches(S8("GITHUB_RUN_ID"),values[3]) &&
        compiler_experiment_job_clock_matches(S8("GITHUB_RUN_ATTEMPT"),values[4]) &&
        compiler_experiment_job_clock_matches(S8("GITHUB_SHA"),values[5]) &&
        compiler_experiment_job_clock_matches(S8("GITHUB_JOB"),expected_kind) &&
        compiler_experiment_job_clock_matches(S8("RUNNER_NAME"),values[9]);
    u64 now=0;
    valid = valid && compiler_experiment_job_clock_now(&now) && now>=observer_finished &&
        now-observer_finished<=120000000ull && now>=lower && now-lower<5400000000ull;
    CompilerExperimentJobClock result = {0};
    if (valid)
    {
        result.record=string_duplicate_arena(arena,record,false);
        result.kind=values[1]; result.run_id=values[3]; result.job_id=values[6]; result.runner_name=values[9];
        result.policy_trusted_revision=values[5]; result.job_started_us=started; result.start_lower_us=lower;
        result.observer_started_us=observer_started; result.observer_finished_us=observer_finished;
        result.entry_realtime_us=now; result.entry_monotonic_us=os_now_microseconds(); result.entry_elapsed_us=now-lower;
        result.valid=true;
        *output=result;
    }
    return valid;
}

BUSTER_GLOBAL_LOCAL bool compiler_experiment_job_clock_resolve(Arena* arena, String8 kind,
    String8 policy_trusted_revision, CompilerExperimentJobClock* output)
{
    bool present=false;
    String8 encoded=compiler_experiment_job_clock_environment(S8("BQ_PHYSICAL_JOB_DATA"),&present), decoded={0};
    bool valid=present && encoded.length<=16384 &&
        compiler_sampling_controller_base64(arena,encoded,false,&decoded);
    return valid && compiler_experiment_job_clock_parse(arena,decoded,kind,policy_trusted_revision,output);
}

// Both budgets start at the conservative lower Actions timestamp, rather than
// public native entry. MIN(whole,worker) reserves the caller's declared tail.
BUSTER_GLOBAL_LOCAL u64 compiler_experiment_job_clock_remaining_us(CompilerExperimentJobClock clock,
    u64 whole_budget_us, u64 worker_budget_us)
{
    u64 result=0,realtime=0,monotonic=os_now_microseconds();
    bool valid=clock.valid && whole_budget_us && worker_budget_us<=whole_budget_us &&
        compiler_experiment_job_clock_now(&realtime) && realtime>=clock.entry_realtime_us &&
        monotonic>=clock.entry_monotonic_us;
    if (valid)
    {
        u64 real_delta=realtime-clock.entry_realtime_us,mono_delta=monotonic-clock.entry_monotonic_us;
        u64 disagreement=real_delta>mono_delta ? real_delta-mono_delta : mono_delta-real_delta;
        valid=disagreement<=2000000ull && mono_delta<=UINT64_MAX-clock.entry_elapsed_us;
        if (valid)
        {
            u64 elapsed=clock.entry_elapsed_us+mono_delta,limit=worker_budget_us<whole_budget_us ? worker_budget_us : whole_budget_us;
            if (elapsed<limit) result=limit-elapsed;
        }
    }
    return result;
}

#endif
