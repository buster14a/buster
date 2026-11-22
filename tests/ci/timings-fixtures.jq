# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Copyright (c) 2026 David Gonzalez Martin. See LICENSE and LICENSING.md.
# Synthetic API responses. Values are fixture data, not a statistics oracle.
("2026-11-03T01:00:00Z" | fromdateiso8601) as $base |
def at($s): if $s == null then null else ($base + $s | todateiso8601) end;
def step($n; $name; $a; $b; $outcome):
    {number:$n,name:$name,status:(if $b == null then "in_progress" else "completed" end),conclusion:$outcome,started_at:at($a),completed_at:at($b)};
def job($id; $name; $a; $b; $outcome; $runner; $labels; $steps):
    {id:$id,name:$name,status:(if $b == null then "in_progress" else "completed" end),conclusion:$outcome,runner_id:$runner,labels:$labels,created_at:at($a - 10),started_at:at($a),completed_at:at($b),steps:$steps};
def run($id; $event; $workflow; $created; $end; $outcome):
    {id:$id,event:$event,workflow:$workflow,status:(if $end == null then "in_progress" else "completed" end),conclusion:$outcome,
     run_attempt:1,head_sha:("000000000000000000000000000000000000000" + ($id | tostring))[-40:],
     head_branch:(if $event == "merge_group" then "gh-readonly-queue/main/pr-" + ($id | tostring) + "-base" elif $event == "pull_request" then "fixture-pr" else "main" end),
     pull_requests:(if $event == "pull_request" then [{number:(if $id == 1 then 41 else 42 end)}] else [] end),
     created_at:at($created),run_started_at:at($created + 1),updated_at:at($end),display_title:"fixture"};
{
  runs:[
    (run(1;"pull_request";"9700x.yml";0;65;"success") | .run_attempt=2 | .run_started_at=at(30)),
    run(2;"pull_request";"9700x.yml";60;115;"cancelled"),
    run(3;"schedule";"9700x.yml";120;190;"timed_out"),
    run(4;"merge_group";"9700x.yml";300;350;"success"),
    run(5;"merge_group";"9700x.yml";400;450;"success"),
    run(6;"merge_group";"9700x.yml";500;550;"cancelled"),
    run(7;"pull_request";"9700x.yml";240;250;"success"),
    run(8;"merge_group";"9700x.yml";600;650;"success"),
    run(9;"workflow_dispatch";"publish-hosted-inputs.yml";700;750;"failure"),
    run(10;"workflow_dispatch";"ci-timings.yml";800;null;null),
    run(11;"pull_request";"build.yml";900;950;"failure"),
    run(12;"merge_group";"9700x.yml";950;951;"skipped"),
    (run(13;"merge_group";"9700x.yml";960;990;"success") | .head_branch="gh-readonly-queue/merge-queue-drill/pr-13-base")
  ],
  attempts:{"1-1":(run(1;"pull_request";"9700x.yml";0;20;"failure"))},
  jobs:{
    "1-1":[job(11;"Build failed";10;20;"failure";1;["ubuntu-26.04"];[step(1;"Build the compiler";10;20;"failure")])],
    "1-2":[job(12;"Hosted correctness";30;65;"success";2;["ubuntu-26.04"];[
      step(1;"Report host CPU and selected compiler path";30;30;"success"),
      step(2;"Build the compiler";30;40;"success"),
      step(3;"Run correctness tests within the hosted CPU budget";40;60;"success"),
      step(4;"Upload artifact";60;65;"success")])],
    "2-1":[job(21;"Cancelled work";70;115;"cancelled";3;["ubuntu-26.04"];[
      step(1;"Build the compiler";70;90;"success"),step(2;"Run correctness tests";90;null;"cancelled")]),
      (job(22;"Missing job start";70;100;"failure";22;["ubuntu-26.04"];[step(1;"Compare missing timestamp";null;90;"failure")]) | .started_at=null)],
    "3-1":([range(100) | job(3000+.;"Skipped hardware";151;150;"skipped";0;["self-hosted"];[])] + [
      job(31;"Native tests and benchmarks on Benchpress (Ryzen 7 9700X)";130;190;"timed_out";31;["self-hosted","buster-9700x"];[
        step(1;"Set up runner";130;140;"success"),step(2;"Compare compiler throughput";140;160;"success"),
        step(3;"Time source corpus";150;170;"failure"),step(4;"Attribute compiler phases";165;175;"timed_out"),
        step(5;"Benchmark skipped";175;185;"skipped")])]),
    "4-1":[job(41;"Queue correctness";310;350;"success";41;["ubuntu-26.04"];[step(1;"Check queue correctness";310;350;"success")])],
    "5-1":[job(51;"Queue correctness";410;450;"success";51;["ubuntu-26.04"];[step(1;"Check queue correctness";410;450;"success")])],
    "6-1":[job(61;"Integration qualification";510;550;"failure";61;["ubuntu-26.04"];[step(1;"Require qualification";510;550;"failure")])],
    "7-1":[job(71;"Skipped";251;250;"skipped";0;["ubuntu-26.04"];[])],
    "8-1":[job(81;"Queue correctness";610;650;"success";81;["ubuntu-26.04"];[step(1;"Check queue correctness";610;650;"success")])],
    "9-1":[job(91;"Prepare";710;750;"failure";91;["ubuntu-26.04"];[step(1;"Acquire toolchain inputs";710;750;"failure")])],
    "10-1":[job(101;"Partial reporter";810;null;null;101;["ubuntu-26.04"];[step(1;"Build report tool";810;820;"success"),step(2;"Collect report";820;null;null)])],
    "11-1":[(job(111;"Missing timestamps";910;950;"failure";null;[];null) | .started_at=null)],
    "12-1":[(job(121;"Cancelled before allocation";952;951;"cancelled";null;["self-hosted"];[]) | .runner_name=null | .runner_group_id=null)],
    "13-1":[job(131;"Disposable queue drill";970;990;"success";131;["ubuntu-26.04"];[step(1;"Check queue drill";970;990;"success")])]
  },
  compares:{"4":"diverged","5":"diverged","8":"ahead","12":"behind"},
  branches:{"4":"same","5":"missing","12":"moved"},
  nightly:"## Nightly main measurements: cycle 2026-11-03\nCarried over: 3 states; the oldest is `fixture`, committed 12.5 hours ago.\nCapacity: 4 code states a day arrived; 2 completed per cycle.\n"
}
