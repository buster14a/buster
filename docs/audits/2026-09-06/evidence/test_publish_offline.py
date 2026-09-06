"""Offline tests only: no GitHub action or subprocess is executed."""
import contextlib
import importlib.util
import io
from pathlib import Path
import unittest
from unittest.mock import patch

BUNDLE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('audit_publish', BUNDLE / 'publish.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PublishOfflineTests(unittest.TestCase):
    def test_patch_hashes(self):
        module.validate_bundle()

    def test_issue_url(self):
        result = module.published_item('https://github.com/buster14a/buster/issues/123\n', 'issues')
        self.assertEqual(result['number'], 123)

    def test_pr_url(self):
        result = module.published_item('https://github.com/buster14a/buster/pull/456\n', 'pull')
        self.assertEqual(result['number'], 456)

    def test_foreign_url_rejected(self):
        with self.assertRaises(RuntimeError):
            module.published_item('https://github.com/other/repo/issues/123', 'issues')

    def test_no_issue(self):
        self.assertIsNone(module.existing_issue(module.MANIFEST['issues'][0], []))

    def test_reuse_open_issue(self):
        item = module.MANIFEST['issues'][0]
        remote = dict(title=item['title'], state='open', body='', html_url='example')
        self.assertEqual(module.existing_issue(item, [remote]), remote)

    def test_closed_issue_rejected(self):
        item = module.MANIFEST['issues'][0]
        remote = dict(title=item['title'], state='closed', body='', html_url='example')
        with self.assertRaises(RuntimeError):
            module.existing_issue(item, [remote])

    def test_duplicate_issue_rejected(self):
        item = module.MANIFEST['issues'][0]
        remote = dict(title=item['title'], state='open', body='', html_url='example')
        with self.assertRaises(RuntimeError):
            module.existing_issue(item, [remote, remote])

    def test_reuse_open_pr(self):
        item = module.MANIFEST['pull_requests'][0]
        remote = dict(head_repo=module.REPO, head_ref=item['branch'], state='open',
                      body=item['marker'], html_url='example')
        self.assertEqual(module.existing_pr(item, [remote]), remote)

    def test_unrelated_pr_rejected(self):
        item = module.MANIFEST['pull_requests'][0]
        remote = dict(head_repo=module.REPO, head_ref=item['branch'], state='open', body='', html_url='example')
        with self.assertRaises(RuntimeError):
            module.existing_pr(item, [remote])

    def test_default_does_not_invoke_commands(self):
        output = io.StringIO()
        with patch.object(module, 'command') as command, patch.object(module.sys, 'argv', ['publish.py']):
            with contextlib.redirect_stdout(output):
                result = module.main()
            command.assert_not_called()
        self.assertEqual(result, 0)
        self.assertIn('OFFLINE PLAN ONLY', output.getvalue())


if __name__ == '__main__':
    unittest.main(verbosity=2)
