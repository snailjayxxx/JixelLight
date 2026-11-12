import copy
import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ValidateDiagnosticReplay import check_replay
from ValidateDiagnostic import validate_diagnostic


class DiagnosticReplayEvidenceTests(unittest.TestCase):
    def fixture(self):
        source=str(Path(tempfile.gettempdir())/'owned-replay-source.png')
        native={'engine':'test-engine','parameter_revision':123,'owned_source_path':source}
        plan={'schema':1,'engine':'test-engine','commands':[
            {'command':'photo.import','photo':'snapshot','source':source},
            {'command':'edit.batch','commands':[{'command':'develop.reset'}]},
            {'command':'export.run','destination':'replay.png','space':'srgb'}]}
        info={'schema':1,'available':True,'engine':'test-engine','parameter_revision':'123','source':source,
              'source_is_RAW':False,'source_content_verified_at_capture':False,'source_hash_enforced':False,
              'original_file_included':False,'catalog_writes':False,'source_writes':False,
              'stored_intent_sha256':'a'*64,'session_entry':'replay_session.json','cli_option':'--session'}
        return native,plan,info

    def check(self,native,plan,info,change=None):
        payload=json.dumps(plan).encode();info=copy.deepcopy(info)
        info.update(session_sha256=hashlib.sha256(payload).hexdigest(),session_bytes=len(payload))
        if change:change(info)
        memory=io.BytesIO()
        with zipfile.ZipFile(memory,'w') as archive:
            archive.writestr('command_replay.json',json.dumps(info));archive.writestr('replay_session.json',payload)
        with zipfile.ZipFile(io.BytesIO(memory.getvalue())) as archive:
            return check_replay(archive,{'command_replay':info},native)

    def test_complete_frozen_session_has_exact_original_bytes(self):
        native,plan,info=self.fixture();checked,payload=self.check(native,plan,info)
        self.assertEqual(json.loads(payload),plan);self.assertFalse(checked['source_hash_enforced'])

    def test_changed_revision_or_source_is_not_attributed_to_snapshot(self):
        native,plan,info=self.fixture()
        for key,value in [('parameter_revision',124),('owned_source_path','another.png')]:
            bad=copy.deepcopy(native);bad[key]=value
            with self.assertRaises(ValueError):self.check(bad,plan,info)

    def test_digest_and_claimed_byte_count_are_independent(self):
        native,plan,info=self.fixture()
        for key,value in [('session_sha256','0'*64),('session_bytes',1)]:
            with self.assertRaises(ValueError):self.check(native,plan,info,lambda d:d.__setitem__(key,value))

    def test_rehashed_partial_or_redirected_plans_are_rejected(self):
        native,plan,info=self.fixture()
        mutations=[lambda p:p.__setitem__('schema',True),lambda p:p.__setitem__('extra',1),
                   lambda p:p['commands'][0].__setitem__('source','different.png'),
                   lambda p:p['commands'][1].__setitem__('commands',[]),
                   lambda p:p['commands'][2].__setitem__('destination',str(Path(tempfile.gettempdir())/'outside.png'))]
        for mutate in mutations:
            bad=copy.deepcopy(plan);mutate(bad)
            with self.assertRaises(ValueError):self.check(native,bad,info)

    def test_false_source_hash_or_write_guarantees_are_rejected(self):
        native,plan,info=self.fixture()
        for key in ['source_hash_enforced','source_content_verified_at_capture','original_file_included','catalog_writes','source_writes']:
            bad=copy.deepcopy(info);bad[key]=True
            with self.assertRaises(ValueError):self.check(native,plan,bad)

    def test_existing_evidence_is_preserved_before_any_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            reports=Path(directory);original=reports/'diagnostic-cpu.json';original.write_bytes(b'original failed evidence')
            result=validate_diagnostic(Path('missing-app'),{},reports,reports,'cpu')
            self.assertFalse(result['passed']);self.assertIn('namespace',result['error'])
            self.assertEqual(original.read_bytes(),b'original failed evidence');self.assertEqual(list(reports.iterdir()),[original])

if __name__=='__main__':unittest.main()
