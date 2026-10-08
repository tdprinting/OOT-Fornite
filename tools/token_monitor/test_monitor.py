import importlib.util
from contextlib import closing
from pathlib import Path
import sqlite3
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("token_monitor_server", Path(__file__).with_name("server.py"))
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)


class MonitorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.db = Path(self.temp.name) / "history.db"
        with closing(sqlite3.connect(self.db)) as db, db:
            db.execute("CREATE TABLE commands (id INTEGER PRIMARY KEY,timestamp TEXT,input_tokens INT,"
                       "output_tokens INT,saved_tokens INT,project_path TEXT)")
            db.executemany("INSERT INTO commands VALUES (?,?,?,?,?,?)", [
                (1, "now", 100, 40, 60, r"\\?\C:\project"),
                (2, "now", 200, 50, 150, r"C:\project\feature-worktree"),
                (3, "now", 500, 100, 400, r"C:\project-other"),
            ])
        self.metrics = server.Metrics(self.db, r"C:\project")

    def tearDown(self):
        self.temp.cleanup()

    def test_workspace_and_sibling_boundary(self):
        data = self.metrics.snapshot()
        self.assertEqual(data["total"]["saved"], 210)
        self.assertEqual(data["total"]["commands"], 2)
        self.assertEqual(data["session"]["saved"], 0)
        self.assertEqual(data["monitor_ai_tokens"], 0)
        self.assertNotIn("project_path", data["recent"][0])

    def test_session_reset_does_not_erase_history(self):
        with closing(sqlite3.connect(self.db)) as db, db:
            db.execute("INSERT INTO commands VALUES (4,'later',90,30,60,'C:/project')")
        self.assertEqual(self.metrics.snapshot()["session"]["saved"], 60)
        self.metrics.reset()
        self.assertEqual(self.metrics.snapshot()["session"]["saved"], 0)
        self.assertEqual(self.metrics.snapshot()["total"]["saved"], 270)
        with closing(sqlite3.connect(self.db)) as db:
            self.assertEqual(db.execute("SELECT COUNT(*) FROM commands").fetchone()[0], 4)

    def test_global_and_missing_database(self):
        self.assertEqual(self.metrics.snapshot("global")["total"]["saved"], 610)
        missing = server.Metrics(Path(self.temp.name) / "missing.db", r"C:\project")
        self.assertEqual(missing.snapshot()["total"]["saved"], 0)
        self.assertTrue(missing.snapshot()["error"])
        self.assertFalse(missing.database.exists())


if __name__ == "__main__":
    unittest.main()
