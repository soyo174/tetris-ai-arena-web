import Control.Monad (forM_)
import Data.Char (isDigit, isSpace)
import Data.List (find, isPrefixOf, tails)
import System.IO (BufferMode(LineBuffering), hSetBuffering, stdout)

requestId :: String -> Maybe String
requestId line = do
    let key = "\"request_id\""
    field <- find (isPrefixOf key) (tails line)
    ':' : value <- pure (dropWhile isSpace (drop (length key) field))
    '"' : quoted <- pure (dropWhile isSpace value)
    let (ident, suffix) = span isDigit quoted
    case suffix of
        '"' : _ | not (null ident) -> Just ident
        _ -> Nothing

main :: IO ()
main = do
    hSetBuffering stdout LineBuffering
    input <- getContents
    forM_ (lines input) $ \line ->
        forM_ (requestId line) $ \ident ->
            putStrLn ("{\"type\":\"PLAY\",\"request_id\":\"" ++ ident
                      ++ "\",\"commands\":[\"HARD_DROP\"]}")
