#!/usr/bin/env python3
"""Public physical-clock reads retain the existing API boundary and byte cap."""
import unittest
from unittest import mock
import compiler_github

class PhysicalClockApiTest(unittest.TestCase):
    def response(self, payload=b'{}'):
        response = mock.MagicMock()
        response.__enter__.return_value = response
        response.read.side_effect = lambda count=-1: payload if count < 0 else payload[:count]
        return response

    def test_public_get_has_no_authorization_and_keeps_exact_response_bound(self):
        response = self.response()
        with mock.patch.object(compiler_github.urllib.request, "urlopen", return_value=response) as opened:
            self.assertEqual(compiler_github.Api("buster14a/buster", "", response_limit=128).request("/actions/runs/91"), {})
        request = opened.call_args.args[0]
        self.assertNotIn("Authorization", request.headers)
        self.assertEqual(request.method, "GET")
        self.assertEqual(request.full_url, "https://api.github.com/repos/buster14a/buster/actions/runs/91")
        response.read.assert_called_once_with(129)

    def test_public_data_reader_refuses_write_and_oversized_body(self):
        api = compiler_github.Api("buster14a/buster", "", response_limit=2)
        with mock.patch.object(compiler_github.urllib.request, "urlopen", return_value=self.response(b'{}x')) as opened:
            with self.assertRaises(ValueError):
                api.request("/actions/runs/91")
        self.assertEqual(opened.call_count, 1)
        with mock.patch.object(compiler_github.urllib.request, "urlopen") as opened:
            with self.assertRaises(ValueError):
                api.request("/check-runs", {"name": "unexpected"})
        opened.assert_not_called()

    def test_existing_authenticated_reader_remains_unbounded_and_authenticated(self):
        response = self.response()
        with mock.patch.object(compiler_github.urllib.request, "urlopen", return_value=response) as opened:
            self.assertEqual(compiler_github.Api("buster14a/buster", "owned-token").request("/actions/runs/91"), {})
        self.assertEqual(opened.call_args.args[0].headers["Authorization"], "Bearer owned-token")
        response.read.assert_called_once_with()

    def test_response_bound_requires_a_positive_bounded_integer(self):
        for bound in (True, 0, -1, "128", 1.0, (8 << 20) + 1):
            with self.subTest(bound=bound), self.assertRaises(ValueError):
                compiler_github.Api("buster14a/buster", "", response_limit=bound)

if __name__ == "__main__":
    unittest.main()
