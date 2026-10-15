import collections,copy,pathlib,tempfile,unittest
from analyze_trace import analyze,main

class TraceControls(unittest.TestCase):
    def seed(self):
        return {'blocks':{0:[0,1],1:[1,1]},'events':collections.defaultdict(list),
                'boundary':[(20,4,2,7,0)],'definitions':{7:0},
                'entry':collections.defaultdict(set),'out':collections.defaultdict(set),
                'pred':collections.defaultdict(list,{1:[0]}),'expected':[0,1]}
    def test_explicit_losses_and_local_absence(self):
        for code,cause in [(2,'call_flush'),(3,'pressure_eviction'),(4,'forced_eviction'),
                           (5,'edge_publication_release'),(6,'contract_replacement'),(7,'tied_source_release'),
                           (0,'release_unattributed')]:
            a=self.seed();a['events'][1,7]=[(4,10,code)]
            self.assertEqual(analyze(a)[2],{cause:1})
        a=self.seed();self.assertEqual(analyze(a)[2],{'never_loaded_in_source_block':1})
        a['out'][0].add(7);self.assertEqual(analyze(a)[2],{'contract_intersection':1})
    def test_same_point_order_does_not_see_future_repair(self):
        a=self.seed();a['events'][1,7]=[(4,10,2),(4,30,0)]
        self.assertEqual(analyze(a)[2],{'call_flush':1})
    def test_selected_attempt_and_predicate_alias(self):
        text='''R A 1 1 1
R P 1 0 0 1
R B 1 0 2 0 4294967295
R Z 1 0xaa 0 1
R A 2 1 1
R P 2 0 0 1
R Z 2 0xbb 0 0
R T 0xaa 0xcc
R F 0xcc
'''
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/'trace';p.write_text(text)
            self.assertEqual(main(p)['totals'],[0,1])
    def test_split_store_and_bad_reload_reconciliation(self):
        a=self.seed();a['expected']=[1,1]
        self.assertEqual(analyze(a)[1],{'quality_split_landing_store':1})
        a['expected']=[1,2]
        with self.assertRaises(AssertionError):analyze(a)

if __name__=='__main__':unittest.main()
